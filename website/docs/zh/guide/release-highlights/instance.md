# 能力实例

能力实例是 AbilityFramework 运行时模型的核心。它把"能力模板（CR）"和"正在跑的进程"解耦：模板是静态声明，实例是模板在某个时刻派生出来的、拥有独立 `instance_id` 的运行时对象，退出即销毁。这套设计让框架可以在运行中反复增删实例，而不需要修改模板本身。

简单说：CR 模板像类（class），能力实例像对象（object）。`POST /api/instance` 相当于 `new`，实例退出相当于析构。

## 为什么需要实例这个概念

早期版本里，CR 既是模板也是运行实体——启动一个能力就直接操作 CR 行。这带来三个问题：

1. **单进程重启会污染模板表**：每次启停都在 CR 表上改状态，模板的语义被运行时状态搅在一起。
2. **并发实例无支持**：一个 CR 行只能表示一个运行进程，无法跑同一能力的多个副本。
3. **配置漂移**：实例跑起来后，如果有人改了模板的 spec，正在运行的实例到底按旧 spec 还是新 spec 工作？没有明确答案。

实例化机制用一张独立的 `AbilityInstance` 表解决全部三个问题：模板表只管声明、实例表只管运行、`spec_snapshot` 在创建瞬间冻结配置。

```mermaid
C4Context
    title 模板与实例的分离

    Person(dev, "开发者", "声明 CR 模板")
    Person(ops, "运维/SDK", "启动 / 停止实例")

    SystemDb(db, "SQLite", "ability_framework.db")

    System_Boundary(fwk, "AbilityFramework") {
        System(api, "HTTP Server", "/api/instance")
        System(rm, "ResourceManager", "实例 CRUD")
        System(lc, "LifecycleManager", "拉起进程 / 心跳驱动")
    }

    System_Ext(tmpl, "AbilityCRBasic", "模板表 (声明)")
    System_Ext(inst, "AbilityInstance", "实例表 (运行时)")
    System_Ext(proc, "能力进程", "能力二进制")

    Rel(dev, db, "注入 package/yaml 模板")
    Rel(ops, api, "POST /api/instance")
    Rel(api, rm, "create_ability_instance")
    Rel(rm, tmpl, "读模板")
    Rel(rm, inst, "INSERT 新实例行 + 快照")
    Rel(rm, lc, "lifecycle_request start")
    Rel(lc, proc, "spawn 子进程")
    Rel(proc, rm, "心跳上报状态")
```

---

## 数据模型

### `AbilityInstance` 表

这是一张纯运行时表，每个正在启动或运行的实例占一行。实例退出后该行立即删除，不保留历史。

| 字段 | 类型 | 说明 |
|---|---|---|
| `instance_id` | TEXT PK | 运行时实例 UUID，由 `create_ability_instance` 生成，**与模板的 `cr_id` 不同** |
| `cr_id` | TEXT | 所属模板（`AbilityCRBasic.instance_id`），N:1 关联 |
| `cr_name` | TEXT | 对应模板的 `metadata.name` |
| `instance_name` | TEXT | 实例显示名，格式 `<cr_name>-<id前8位>` |
| `ability_name` | TEXT | 能力类名称 |
| `ability_version` | TEXT | 能力版本 |
| `pid` | INTEGER | 进程 PID（预留，暂未使用） |
| `state` | TEXT | 生命周期状态，默认 `Inactive` |
| `start_time` | INTEGER | 启动时间（unix 秒） |
| `stop_time` | INTEGER | 停止时间 |
| `spec_snapshot` | TEXT | 启动瞬间的 AbilityCR JSON 快照 |
| `detail` | TEXT | 额外信息 JSON（`abilityPort`、`IPCPort` 等） |

### 模板与实例的关系（ER 图）

```mermaid
erDiagram
    AbilityCRBasic ||--o{ AbilityInstance : "cr_id 派生"

    AbilityCRBasic {
        TEXT instance_id PK "模板 UUID"
        TEXT cr_name "metadata.name"
        TEXT ability_name
        TEXT ability_version
        INTEGER singleton "1=单例 0=多实例"
        INTEGER autostart
        TEXT cr_detail "完整 CR JSON"
    }

    AbilityInstance {
        TEXT instance_id PK "运行时 UUID (新建)"
        TEXT cr_id FK "指向模板"
        TEXT cr_name
        TEXT instance_name "cr_name-id前8位"
        TEXT ability_name
        TEXT ability_version
        TEXT state "生命周期状态"
        INTEGER start_time
        INTEGER stop_time
        TEXT spec_snapshot "启动瞬间 CR 冻结"
        TEXT detail "端口等额外信息"
    }
```

### `spec_snapshot` —— 配置冻结机制

这是实例化设计中最关键的字段。`create_ability_instance` 在创建瞬间做三件事：

1. 复制模板 CR 为 `snapshot`；
2. 把 `snapshot.id` 改成新生成的 `instance_id`（让下游一致）；
3. 把 `lifecycleState` 置为 `Inactive`，序列化成 JSON 存入 `spec_snapshot`。

这样即使模板之后被修改、删除，正在运行的实例仍然按创建那一刻的 spec 工作。`resolve_ability_cr_by_id` 解析时**优先读 `spec_snapshot`**，解析失败才回退到模板表。

```mermaid
flowchart LR
    TMPL["模板 CR\n(cr_id=A)"] --> COPY["复制副本"]
    COPY --> REN["id 替换为 instance_id=B\nstate=Inactive"]
    REN --> JSON["序列化为 JSON"]
    JSON --> SNAP["存入 spec_snapshot"]
    SNAP -. "运行时冻结" .- FROZEN["实例 B 始终用这份 spec"]

    TMPL2["模板被修改"] -. 不影响 .- FROZEN
    TMPL3["模板被删除"] -. 不影响 .- FROZEN
```

---

## `AbilityInstanceInfo` 结构

ResourceManager 对外暴露的实例元信息结构（`include/resourcemgr/resource_mgr.hpp`）：

```mermaid
classDiagram
    class AbilityInstanceInfo {
        +uuid instance_id
        +optional~uuid~ cr_id
        +string cr_name
        +string instance_name
        +string ability_name
        +string ability_version
        +string state
        +int64 start_time
        +int64 stop_time
        +json detail
        +json spec_snapshot
    }

    class ResourceManager {
        +create_ability_instance(CR) uuid
        +delete_ability_instance(ID)
        +get_ability_instance(ID) optional~Info~
        +get_all_ability_instances() vector~Info~
        +get_active_instances_by_ability_name(name) vector~Info~
        +resolve_ability_cr_by_id(ID) optional~CR~
        +on_event_heartbeat(Heartbeat) bool
    }

    ResourceManager ..> AbilityInstanceInfo : 产出
```

---

## 生命周期状态

实例的 `state` 字段遵循 LifecycleManager 的状态机（见 [生命周期管理器](/guide/concepts/architecture)）。状态枚举定义在 `include/lifecyclemgr/heartbeat.hpp`：

| 状态 | 值 | 含义 |
|---|---|---|
| `Inactive` | 0 | 已创建但未启动 / 已停止 |
| `Init` | 1 | 进程已拉起，正在初始化 |
| `Standby` | 2 | 就绪，IPC 端口已绑定，可接受 connect |
| `Running` | 3 | 正常运行中 |
| `Suspend` | 4 | 挂起 |
| `Terminated` | 5 | 终止（行立即删除） |
| `Error` | 66 | 错误 |
| `Unknown` | 65 | 未知 |

```mermaid
stateDiagram-v2
    [*] --> Inactive : create_ability_instance

    Inactive --> Init : lifecycle start
    Init --> Standby : 心跳上报
    Standby --> Running : lifecycle connect

    Running --> Suspend : 临时挂起
    Suspend --> Running : 恢复

    Running --> Init : 重启
    Standby --> Init : 重启

    Inactive --> Terminated : delete
    Init --> Terminated : terminate / 进程退出
    Standby --> Terminated : terminate / 进程退出
    Running --> Terminated : terminate / 进程退出
    Suspend --> Terminated : terminate / 进程退出

    Terminated --> [*] : delete_ability_instance
    Error --> [*] : delete_ability_instance
```

> 关键语义：一旦实例进入 `Terminated`，`on_event_heartbeat` 会立即删除该行。实例表里不存在 `Terminated` 状态的残留行——它只是一个瞬态信号，触发删除。

---

## 创建实例的完整链路

`POST /api/instance` 是最核心的入口。它接收一个模板引用（名字或 UUID），派生实例，然后根据 `start` / `connect` 参数决定拉起多深的任务链。

### 创建时序图

```mermaid
sequenceDiagram
    participant U as 调用方
    participant API as HTTP Server
    participant RM as ResourceManager
    participant TM as TaskManager
    participant LC as LifecycleManager
    participant SP as SubprocessManager
    participant AB as 能力进程

    U->>API: POST /api/instance {template, start, connect}
    API->>RM: find_template(ref)
    RM-->>API: AbilityCR 模板

    API->>RM: 单例检查 get_active_instances_by_ability_name
    alt 已有运行实例
        RM-->>API: 非空列表
        API-->>U: 409 singleton constraint
    end

    API->>API: 构造任务链 prepare_task_start_ability
    API->>TM: submit_task(sequence)
    API-->>U: 200 {taskId, template} (异步)

    Note over TM: 任务链异步执行

    TM->>RM: task 1: create_ability_instance(模板)
    RM->>RM: 生成 instance_id + 存 spec_snapshot
    RM-->>TM: instance_id

    opt start=true
        TM->>LC: task 2: lifecycle_request start
        LC->>SP: start_process (带退出回调)
        SP->>AB: spawn 子进程
        AB-->>LC: 首次心跳 (Init/Standby)

        opt connect=true
            TM->>RM: task 3: wait-standby (轮询最长 30s)
            loop 每 0.x 秒
                RM->>RM: get_ability_instance(state)
            end
            TM->>LC: task 4: lifecycle_request connect
            AB-->>LC: 心跳 Running
        end
    end
```

### 任务链的四种变体

`prepare_task_start_ability`（`src/resourcemgr/resource_mgr_http_apis.cpp`）根据参数组合出不同的 `tasks::sequence`：

```mermaid
flowchart TD
    START["prepare_task_start_ability(cr, start, connect)"] --> T1["task 1: create-instance\n生成 instance_id + spec_snapshot"]
    T1 --> CHK1{"start=false 且 connect=false?"}
    CHK1 -- 是 --> NOOP["task: noop-start\n仅创建实例，不拉进程"]
    NOOP --> DONE1["返回"]

    CHK1 -- 否 --> T2["task 2: start-ability\nlifecycle_request start"]
    T2 --> CHK2{"connect=false?"}
    CHK2 -- 是 --> DONE2["返回 (停在 Init/Standby)"]
    CHK2 -- 否 --> T3["task 3: wait-standby\n轮询 state==Standby/Running 最长 30s"]
    T3 --> T4["task 4: connect-ability\nlifecycle_request connect"]
    T4 --> DONE3["返回 (到达 Running)"]
```

| 调用参数 | 任务链 | 最终状态 |
|---|---|---|
| `start=false, connect=false` | create-instance → noop-start | Inactive（仅创建行） |
| `start=true, connect=false` | create-instance → start-ability | Init 或 Standby |
| `start=true, connect=true` | create-instance → start-ability → wait-standby → connect-ability | Running |
| `start=false, connect=true` | 同上（connect 隐含 start） | Running |

> `connect` 为 true 时强制 `start=true`（代码：`bool start = body.value("start", true); bool connect = start && body.value("connect", true);`）。

---

## 单例约束

当模板的 `spec.singleton` 为 true（缺省值）时，同一能力名全局只允许一个活跃实例。`POST /api/instance` 在创建前会检查：

```mermaid
flowchart TD
    POST["POST /api/instance"] --> FIND["find_template(template)"]
    FIND -->|找不到| NF["404 template not found"]
    FIND -->|找到| SG{"spec.singleton?"}
    SG -->|false| GO["继续创建"]
    SG -->|true 缺省| ACTIVE["get_active_instances_by_ability_name"]
    ACTIVE --> EMPTY{"活跃实例列表为空?"}
    EMPTY -- 否 --> CONFLICT["409 singleton constraint\n返回 running_instance_id"]
    EMPTY -- 是 --> GO
    GO --> TASK["提交任务链"]
```

`get_active_instances_by_ability_name` 查询的是 `state NOT IN ('Inactive', 'Terminated')` 的行，即 Init / Standby / Running / Suspend 都算活跃。

---

## use_remote_execution —— 远程执行模式

配置项 `/use_remote_execution`（默认 `false`）控制启动路径。当为 true 时，`POST /api/instance` 跳过 manifest 查找和包下载，直接走 `make_task_start_ability`：

```mermaid
flowchart TD
    POST["POST /api/instance"] --> SINGLETON["单例检查通过"]
    SINGLETON --> RE{"use_remote_execution?"}

    RE -- true --> DIRECT["make_task_start_ability\n跳过 manifest / 包下载\n能力进程由 zenoh 远程启动"]
    DIRECT --> SUBMIT["submit_task"]

    RE -- false --> MF["get_ability_manifest(name, version)"]
    MF --> HAS{"manifest 存在?"}
    HAS -- 是 --> START["make_task_start_ability"]
    HAS -- 否 --> DL["make_task_download_pkg_and_start_ability\ntask_download_package + start 链"]
    START --> SUBMIT
    DL --> SUBMIT
```

这适用于节点本身没有本地 package 目录的场景——能力进程通过 zenoh 协议在远端节点上启动，本地框架只负责发 lifecycle_request 和维护心跳状态。

---

## 心跳与状态更新

能力进程启动后，通过 `POST /api/ability-heartbeat` 周期性上报 `Heartbeat`（含 `id`、`state`、`abilityPort`、`IPCPort`）。心跳到达后由 LifecycleManager 转发给 ResourceManager 的 `on_event_heartbeat`：

```mermaid
flowchart TD
    HB["心跳 Heartbeat\n{id, state, ports}"] --> TERM{"state == Terminated?"}
    TERM -- 是 --> DEL["delete_ability_instance(id)\n删除行并返回 true"]
    TERM -- 否 --> UPD["UPDATE AbilityInstance\nSET state=?, detail=ports"]
    UPD --> ROW{"更新命中行数 > 0?"}
    ROW -- 是 --> OK["return true\n心跳被接受"]
    ROW -- 否 --> DROP["return false\n孤儿心跳，被丢弃"]
    DROP --> GONE["HTTP 层回 410 Gone"]
    GONE --> SDK["SDK 连续收到 410 后自毁"]
```

### 孤儿心跳的治理

历史上 `on_event_heartbeat` 在 UPDATE 命中 0 行时会 INSERT 一条"孤儿心跳"占位行。这个 fallback 在框架重启后会产生 **ghost 实例**：上一代被 reparent 到 init 的 python 僵尸进程仍在发心跳，框架给它们造出与任何模板无关、`spec_snapshot` 为 null 的 Running 行，调试非常困难。

现在的修法是**直接丢弃孤儿心跳**，返回 false 让 LifecycleManager 的 HTTP 层回 `410 Gone`，SDK 侧连续收到 410 后会自毁。日志用 `LOG_FIRST_N(WARNING, 20)` 限流，只打前 20 条。

---

## 终止实例

`DELETE /api/instance/:id` 负责销毁实例。它的逻辑是"先尝试优雅终止，再兜底删行"：

```mermaid
flowchart TD
    DEL["DELETE /api/instance/:id"] --> GET["get_ability_instance(id)"]
    GET --> FOUND{"实例存在?"}
    FOUND -- 否 --> NF["404 instance not found"]
    FOUND -- 是 --> ACT{"state 活跃?\n(非 Inactive/Terminated)"}
    ACT -- 是 --> TERM["send_sync lifecycle_request terminate"]
    ACT -- 否 --> SKIP["跳过 terminate"]
    TERM --> CLEAN["delete_ability_instance(id) 兜底删行"]
    SKIP --> CLEAN
    CLEAN --> OK["200 {status: ok}"]
```

terminate 命令发给 LifecycleManager 后，正常情况下心跳回调或进程退出回调会触发删行。但 `delete_ability_instance` 作为兜底保证即使 lifecycle 没及时回调，行也会被清掉。

---

## 启动时的陈旧实例清理

框架重启后无法验证旧实例是否存活（`PR_SET_PDEATHSIG` + SubprocessManager 已经杀掉了孤儿进程），所以 `ResourceManager::init()` 会把所有非终止状态的行标记为 Terminated：

```sql
UPDATE AbilityInstance
SET state = 'Terminated', stop_time = ?
WHERE state NOT IN ('Terminated', 'Inactive')
```

这一步解决了两个问题：

1. 单例检查 `get_active_instances_by_ability_name` 不会误以为有运行中实例；
2. WebUI 的"运行实例"面板不会显示陈旧的 Running 行。

```mermaid
flowchart TD
    BOOT["框架启动 init()"] --> SCAN["扫描 AbilityInstance 表"]
    SCAN --> LOOP{"遍历每行"}
    LOOP --> ACTIVE{"state NOT IN\n(Terminated, Inactive)?"}
    ACTIVE -- 是 --> MARK["标记为 Terminated\n记录 stop_time"]
    ACTIVE -- 否 --> KEEP["保持原样"]
    MARK --> LOOP
    KEEP --> LOOP
    LOOP --> DONE["清理完成\n单例检查 / WebUI 不受残留影响"]
```

同时还会重置 `AbilityCRBasic.auto_started = 0`，因为 autoStart 的语义是"每次框架启动自动拉起一次"，而不是"CR 整个生命周期只拉起一次"。

---

## autoStart —— 启动时自动拉起

标记了 `autostart = 1` 的 CR 模板会在框架启动时自动派生并启动实例。流程由 `ResourceManager::auto_start_ability()` 驱动：

```mermaid
sequenceDiagram
    participant Boot as 框架启动
    participant RM as ResourceManager
    participant TM as TaskManager
    participant LC as LifecycleManager

    Boot->>RM: update() 触发
    RM->>RM: 查询 autostart=1 且 auto_started=0 的 CR
    loop 每个 autostart CR
        RM->>RM: create_ability_instance(模板)
        RM->>RM: 启动次数检查 retry_count < 3
        RM->>LC: lifecycle_request start
        RM->>RM: db_mark_auto_started(id)
    end
```

crash 防护由内存中的 `start_records` 提供：同一会话内某个能力的重试次数超过 3 次就不再自动拉起，避免崩溃循环。

---

## HTTP API 速查

| 方法 | 路径 | 说明 | 关键状态码 |
|---|---|---|---|
| GET | `/api/instance` | 列出所有实例 | 200 |
| GET | `/api/instance/:id` | 查询单个实例 | 200 / 404 |
| POST | `/api/instance` | 从模板派生并启动 | 200 / 404 / 409 |
| DELETE | `/api/instance/:id` | 终止并销毁 | 200 / 404 |

### POST 请求体

```json
{
  "template": "my-ability",
  "start": true,
  "connect": true
}
```

- `template`：必填，CR 名字或 UUID。
- `start`：默认 true，是否拉起进程。
- `connect`：默认 true，是否推进到 Running（隐含 start=true）。

### 返回示例

```json
{
  "taskId": "task-uuid",
  "template": "my-ability"
}
```

创建是异步的——任务链提交给 TaskManager 后立即返回 `taskId`，实际启动进度需要轮询实例状态或查询 TaskStatus。

### GET 响应字段

```json
{
  "instance_id": "uuid",
  "cr_id": "模板uuid",
  "cr_name": "my-ability",
  "instance_name": "my-ability-a1b2c3d4",
  "ability_name": "mover",
  "ability_version": "1.0.0",
  "state": "Running",
  "start_time": 1700000000,
  "stop_time": null,
  "detail": {"abilityPort": 8081, "IPCPort": 8082},
  "spec_snapshot": {}
}
```

---

## 相关参考

- [资源管理器](/guide/concepts/architecture)
- [生命周期管理器](/guide/concepts/architecture)
- [任务管理器](/guide/concepts/architecture)
- [HTTP API 参考](/api/http-api)
- [配置文件参考](/guide/configuration/config-file)
- [消息总线](/guide/concepts/architecture)
