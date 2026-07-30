# HTTP API

> 适用框架版本: **v2.6.0+** &nbsp;|&nbsp; 默认监听: `0.0.0.0:8080` &nbsp;|&nbsp; Content-Type: `application/json`

AbilityFramework 是机器人节点上的能力运行时，通过 HTTP REST API 暴露给上层（Studio / MCP server / 业务集成方）。本文枚举当前版本所有公开 endpoint，按用途分组并给出调用示例。

## 通用约定

| 项       | 约定                                                                                                                                                   |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Base URL | `http://<host>:8080`                                                                                                                                   |
| 认证     | 当前版本**无认证**（局域网/同节点信任）。后续版本计划加 JWT。                                                                                          |
| 编码     | 请求/响应均 UTF-8；Content-Type `application/json`（少数为 `text/plain` / `text/markdown`）                                                            |
| 时间格式 | RFC 3339 UTC（`2026-04-22T10:30:00Z`）                                                                                                                 |
| 错误     | 4xx / 5xx 用 [RFC 7807 Problem Detail](https://datatracker.ietf.org/doc/html/rfc7807) JSON：`{"type":"...","title":"...","status":404,"detail":"..."}` |
| 幂等性   | DELETE 通常幂等（删除不存在不报错）；POST `/api/instance` 受 `singleton` 约束限制；POST `/api/lifecycle-request` 同 command 重复发送会返回相同 taskId  |
| 异步任务 | 多个 POST 接口返回 `{"taskId":"..."}`，调用方轮询 `GET /api/task/:id` 跟踪结果（推荐 200ms 周期，30s 上限）                                            |
| 心跳清理 | 1 分钟内未更新的 `ability-heartbeat` 会被框架视为掉线并清理                                                                                            |

---

## 1. 健康检查与配置

服务存活、配置查询、日志检索。Studio launcher / mcp-server 启动后定期轮询本组接口确认框架可用。

### GET /api/hello

**场景**：liveness probe，最便宜的"框架在线吗"判断。

**响应 200**：

```
this is ability framework
```

```bash
curl http://localhost:8080/api/hello
```

**规范**：无 body；返回 `text/plain`；2xx 即视为可达；< 10ms。

### GET /api/config

**场景**：读取框架运行时配置（端口、framework_name、各子模块开关等）。

**响应 200**：

```json
{
  "http_port": 8080,
  "framework_name": "robot-lab-01",
  "discovery": { "multicast_group": "239.0.0.1", "port": 30001 },
  "...": "..."
}
```

```bash
curl http://localhost:8080/api/config
```

**规范**：内容来自 `config.yaml`，YAML→JSON 自动转换；如配置文件缺失返回 404。

### GET /api/log

**场景**：拉取最近运行日志，用于现场排障（无需 ssh 进节点 cat log）。

**Query**：`lines`（int，默认 100，上限 5000），`level`（`INFO`/`WARNING`/`ERROR`，默认 `INFO`，仅返回 ≥ 该级别的行）。

**响应 200**：

```json
["2026-04-22T03:00:00Z [I] ResourceManager::update()", "..."]
```

```bash
curl 'http://localhost:8080/api/log?lines=50&level=WARNING'
```

**规范**：日志文件不存在时返回 `[]` 而非 404。

---

## 2. 能力清单 (Manifest) 与 CRD 元数据

Manifest 是包作者编写的能力描述（任务列表 + 参数 schema + 配置 schema）；CRD 是框架内置的资源类型规范（用来约束 manifest / CR YAML 的合法性）。

### GET /api/manifest

**场景**：列出框架已加载的所有能力的清单摘要。Studio 用它在"能力商店"页渲染卡片。

**响应 200**：

```json
[
  {
    "abilityName": "EndpointControl.Robotics",
    "kind": "AtomAbility",
    "package": "robotics.endpoint.control",
    "version": "1.0.0",
    "taskCount": 5,
    "hasConfigSchema": true
  }
]
```

```bash
curl http://localhost:8080/api/manifest | jq
```

**规范**：仅元信息；要拿完整 schema 走 `GET /api/manifest/:name/:version`。

### GET /api/manifest/:name/:version

**场景**：取单个能力的完整 manifest，含每个 task 的 input/output schema 和配置 schema。Studio 表单编辑器消费这份 schema。

**响应 200**（截选）：

```json
{
  "abilityName": "EndpointControl.Robotics",
  "version": "1.0.0",
  "tasks": [
    {
      "name": "MoveToPose",
      "parameters": { "type": "object", "properties": { "x": { "type": "number" } } },
      "returns":    { "type": "object" }
    }
  ],
  "configSchema": { "type": "object", "properties": { "robotUri": { "type": "string" } } }
}
```

```bash
curl http://localhost:8080/api/manifest/EndpointControl.Robotics/1.0.0
```

**规范**：版本不存在返回 404。

### GET /api/crd

**场景**：列出所有 CRD（自定义资源类型）。可过滤。

**Query**：`abilityName` / `package` / `kind` (`device` 时只返回设备类 CRD)。

**响应 200**：CRD 数组。

```bash
curl 'http://localhost:8080/api/crd?package=robotics.endpoint.control'
```

**规范**：所有 query 参数 AND 组合。

### GET /api/crd/:name/:version

**场景**：按 name + version 取单个 CRD（含 openAPIV3Schema 完整定义）。

```bash
curl http://localhost:8080/api/crd/AtomAbility/v1
```

**规范**：缺失返回 400 invalid argument。

### GET /api/builtin-crd/ability

**场景**：拿框架自带的 AtomAbility CRD JSON Schema。Studio 用来本地验证 manifest 写得对不对（提前拦截，省去一轮上传）。

```bash
curl http://localhost:8080/api/builtin-crd/ability > ability.crd.schema.json
```

**规范**：返回的是 JSON Schema (Draft-07)；幂等。

### GET /api/builtin-crd/service

**场景**：同上，但拿的是 Service CRD 的 schema。

```bash
curl http://localhost:8080/api/builtin-crd/service > service.crd.schema.json
```

---

## 3. CR 模板（只读）

CR (Custom Resource) 是能力的静态模板，由能力包内置的 `crs/*.yaml` 或工作目录 `crs/*.yaml` 决定。运行时**不能创建 / 修改 / 删除** —— 只能从 CR 派生 instance。

### GET /api/cr

**场景**：列出所有 CR 模板（能力 CR 或 Query 指定的设备 CR）。Studio "实例化" 下拉框的数据源。

**Query**：`abilityName` / `version` / `package` / `nodeId` / `nodeName` / `nodeAddr` / `kind=device`。

**响应 200**：CR 数组。

```bash
curl 'http://localhost:8080/api/cr?abilityName=EndpointControl.Robotics'
```

### GET /api/cr/:id

**场景**：按 UUID 取单个 CR。如果给的 id 是 instance_id，框架会从 instance 的 spec_snapshot 反查回原 CR。SDK 端在自识别"我是哪个 CR"时常用。

```bash
curl http://localhost:8080/api/cr/4f9e2c1a-...
```

**规范**：找不到返回 404。

### GET /api/device_crs

**场景**：只列设备类 CR（避免和能力 CR 混在一起）。

```bash
curl http://localhost:8080/api/device_crs
```

### GET /api/crs

**场景**：列出 `crs/` 目录下所有 YAML 文件（不解析）。Studio "工作区文件管理" 用。

**响应 200**：

```json
{ "files": ["endpoint-control.cr.yaml", "nav.cr.yaml"], "total": 2 }
```

```bash
curl http://localhost:8080/api/crs
```

### POST /api/crs/autostart

**场景**：原子修改 CR YAML 文件的 `spec.autoStart` 字段（开/关启动时自动拉起）。比让用户手编 YAML 安全。

**请求**：

```json
{ "filename": "endpoint-control.cr.yaml", "autoStart": true }
```

**响应 200**：`{"success":true,"filename":"endpoint-control.cr.yaml","autoStart":true}`

```bash
curl -X POST http://localhost:8080/api/crs/autostart \
  -H 'Content-Type: application/json' \
  -d '{"filename":"endpoint-control.cr.yaml","autoStart":true}'
```

**规范**：文件不存在或写盘失败返回 500；不会改 YAML 中的其他字段。

### POST /api/cr  &nbsp;<sup style="color:#f85149">410 Gone</sup>

已废弃。CR 由包/YAML 决定，不再支持运行时创建。要新增 CR，把 YAML 放到 `crs/` 后框架 reconcile 会自动加载。

### DELETE /api/cr/:id  &nbsp;<sup style="color:#f85149">410 Gone</sup>

已废弃。同上，删 YAML 并等 reconcile。

---

## 4. 能力实例生命周期

实例 (Instance) 是 CR 模板的运行时副本。框架按需从 CR 派生 instance、启动子进程、监控、销毁。

### GET /api/instance

**场景**：列出所有运行中或已终止的实例。

**响应 200**：

```json
[
  {
    "instance_id": "uuid",
    "cr_id": "template-uuid",
    "cr_name": "endpoint-control-1",
    "instance_name": "endpoint-control-1-run-1",
    "ability_name": "EndpointControl.Robotics",
    "ability_version": "1.0.0",
    "state": "Running",
    "start_time": "2026-04-22T10:30:00Z",
    "stop_time": null
  }
]
```

```bash
curl http://localhost:8080/api/instance
```

### GET /api/instance/:id

**场景**：单个实例详情，含 `spec_snapshot`（实例化时的 CR 快照，用于审计 / 回溯）。

```bash
curl http://localhost:8080/api/instance/4f9e2c1a-...
```

### POST /api/instance

**场景**：从 CR 模板派生并启动一个实例。这是**最常用的"启能力"接口**——内部自动串起 download-package（如需）→ create → start → wait Standby → connect 全流程。

**请求**：

```json
{
  "template": "endpoint-control-1",
  "start": true,
  "connect": true
}
```

`template` 可以是 CR 名（如 `endpoint-control-1`）或 CR UUID。`start` / `connect` 默认 true。

**响应 200**：`{"taskId":"task-uuid","template":"endpoint-control-1"}`

```bash
curl -X POST http://localhost:8080/api/instance \
  -H 'Content-Type: application/json' \
  -d '{"template":"endpoint-control-1"}'
# → 轮询: curl http://localhost:8080/api/task/<taskId>
```

**规范**：

- 若 CR `spec.singleton=true` 且已有运行实例 → **409 Conflict**。
- taskId 跟踪整个启动管线；最终 state=`finished` 时 result 含 `instance_id`。

### DELETE /api/instance/:id

**场景**：终止并销毁实例。若实例处于 Active，框架先发 lifecycle terminate 再删行。

**响应 200**：`{"status":"ok"}`

```bash
curl -X DELETE http://localhost:8080/api/instance/4f9e2c1a-...
```

**规范**：幂等（不存在的 id 返回 404，但重复删已删的 id 不报错）。

---

## 5. 生命周期细粒度控制

`POST /api/instance` 和 `DELETE /api/instance/:id` 是粗粒度封装；本组接口给精细场景（先 create 不 start、disconnect 后保留实例待重连等）。

### POST /api/lifecycle-request

**场景**：手动驱动实例状态转移。状态机：`Created → Standby ⇄ Active(Running) → Terminated`。

**请求**：

```json
{
  "abilityInstanceId": "instance-uuid",
  "command": "start"
}
```

`command`: `start` | `connect` | `disconnect` | `terminate`。

**响应 200**：`{"taskId":"task-uuid"}`

```bash
curl -X POST http://localhost:8080/api/lifecycle-request \
  -H 'Content-Type: application/json' \
  -d '{"abilityInstanceId":"4f9e2c1a-...","command":"connect"}'
```

**规范**：返回 taskId 后异步等达目标状态，超时 15s。实例不存在返回 404。

### POST /api/ability-heartbeat

**场景**：**SDK 内部使用**，能力子进程定期（默认 5s）向框架报告自己活着。普通客户端不应调。

**请求**（JSON 或 CBOR 都接受）：

```json
{
  "id": "instance-uuid",
  "abilityName": "EndpointControl.Robotics",
  "abilityVersion": "1.0.0",
  "state": "Running",
  "abilityPort": 9002,
  "IPCPort": 9001,
  "IPCProtocol": "http",
  "position": "localhost"
}
```

**响应 200**：`OK` (text/plain)

**响应 410 Gone**：框架不再认识这个 id（实例已被显式销毁），SDK 应自毁退出。

**规范**：>1 分钟未刷新的心跳被自动清理。

### GET /api/ability-heartbeat

**场景**：调试用——看哪些实例当前确实在 reporting。

```bash
curl http://localhost:8080/api/ability-heartbeat
```

### GET /api/ability-heartbeat/:id

**场景**：查单个实例最近一次心跳。

```bash
curl http://localhost:8080/api/ability-heartbeat/4f9e2c1a-...
```

**规范**：未注册实例返回 404。

---

## 6. 能力代理（任务调用）

把 `/api/ability/:id/<rest>` 透明转发到能力进程在 heartbeat 里登记的 `abilityPort`。客户端只跟框架打交道，不用知道每个实例的端口。

### `{GET,POST,PUT,DELETE} /api/ability/:id/:subpath`

**场景**：调用能力实例提供的业务 API。最常见就是 `POST /api/ability/<id>/api/task/start` 启一个 task。

**请求**：透传，body / headers / method 全部转发。

**响应**：透传被代理服务的响应。

```bash
# 启动一个 MoveToPose 任务
curl -X POST http://localhost:8080/api/ability/4f9e2c1a-.../api/task/start \
  -H 'Content-Type: application/json' \
  -d '{"task_type":"MoveToPose","params":{"x":0.5,"y":0.2,"z":0.3}}'

# 列出实例上的所有任务
curl http://localhost:8080/api/ability/4f9e2c1a-.../api/task/list
```

**规范**：实例不存在或未 Running 时返回 404；网络层错误（能力进程崩了）返回 502。

---

## 7. 框架内部任务

异步任务跟踪（启动实例、下载包等都封装为 task）。

### GET /api/task

**场景**：列出所有 task UUID。`?active=true` 只列尚未结束的。

```bash
curl 'http://localhost:8080/api/task?active=true'
```

### GET /api/task/:id

**场景**：查 task 状态——`POST /api/instance` 等异步接口返回 taskId 后用它轮询。

**响应 200**：

```json
{
  "id": "task-uuid",
  "name": "auto-start-ability",
  "state": "finished",
  "error": null,
  "result": { "instance_id": "..." }
}
```

`state`: `running` / `finished` / `error` / `cancelled`。

```bash
curl http://localhost:8080/api/task/task-uuid
```

**规范**：未知 id 返回 error。

### POST /api/task

**场景**：提交框架级自定义 task（要求对应 task_type 已注册工厂）。

**请求**：

```json
{ "task_type": "test.hello", "payload": { "a": 1 } }
```

**响应 200**：`{"taskId":"..."}`

```bash
curl -X POST http://localhost:8080/api/task \
  -H 'Content-Type: application/json' \
  -d '{"task_type":"test.hello","payload":{}}'
```

---

## 8. 能力包管理

上架 / 下架能力包（zip 格式，含 `package.yaml` + `ability.manifest.yaml` + 二进制）。

### POST /api/package

**场景**：上传新能力包（首次上架）或更新已有包。

**请求**：`Content-Type: application/zip`（直接 raw body）或 `multipart/form-data` 字段名 `file`。

**Query**：`force=true` 可强制覆盖已存在的同 name+version 包。

**响应 200**：

```json
{
  "package": "robotics.endpoint.control",
  "version": "1.0.0",
  "crCount": 1,
  "status": "ok"
}
```

```bash
# raw zip
curl -X POST http://localhost:8080/api/package \
  -H 'Content-Type: application/zip' \
  --data-binary @robotics.endpoint.control.zip

# 强制覆盖
curl -X POST 'http://localhost:8080/api/package?force=true' \
  -H 'Content-Type: application/zip' \
  --data-binary @robotics.endpoint.control.zip
```

**规范**：上传后自动 reconcile，包内 CR 镜像入库；体积上限 100MB（超出 413）。

### GET /api/package

**场景**：列出已上传的包（当前实现返回 `[]`，规划中）。

### DELETE /api/package/:name/:version

**场景**：下架包，框架 reconcile 后该包的 CR 被 unmirror，运行实例不受影响。

```bash
curl -X DELETE http://localhost:8080/api/package/robotics.endpoint.control/1.0.0
```

---

## 9. Skill 文档

能力包可携带 `*.md` 给 LLM agent 当行为指引（"调这些 task 的时候做 X"）。MCP server 把这些 skill 暴露成 `skill://` 资源给 Claude/OpenAI agents。

### GET /api/skill

**场景**：列出所有 skill 元数据（路径、大小、首行标题）。

**响应 200**：

```json
[
  {
    "package": "robotics.endpoint.control",
    "version": "1.0.0",
    "filename": "tasks/move-to-pose.md",
    "size_bytes": 1234,
    "title": "MoveToPose"
  }
]
```

```bash
curl http://localhost:8080/api/skill
```

### GET /api/skill/:package/:version/:filename

**场景**：取单个 skill markdown 原文。`:filename` 可带子目录（如 `tasks/move-to-pose.md`）。

```bash
curl 'http://localhost:8080/api/skill/robotics.endpoint.control/1.0.0/tasks/move-to-pose.md'
```

**响应 200**：`text/markdown; charset=utf-8`，body 为 raw markdown。

---

## 10. 资源占用

控制实例间共享 / 独占某个 CR（典型场景：一个机械臂 CR 同时被两个能力共享 vs 独占）。

### GET /api/cr/:id/occupation

**场景**：查谁在占用某个资源。

**响应 200**：

```json
{
  "owner": { "abilityInstanceId": "uuid", "position": "localhost" },
  "sharers": { "instance-uuid-1": "pos1", "instance-uuid-2": "pos2" }
}
```

```bash
curl http://localhost:8080/api/cr/4f9e2c1a-.../occupation
```

### POST /api/cr/:id/occupation

**场景**：申请占用资源（共享 / 独占）。能力启动前由 SDK 或编排层调。

**请求**：

```json
{ "occupy_id": "instance-uuid", "mode": "shared", "position": "localhost" }
```

`mode`: `shared` 或 `unique`。

**响应 200**：`OK` (text/plain)

**响应 409 Conflict**：unique 申请遇到已被占用。

```bash
curl -X POST http://localhost:8080/api/cr/4f9e2c1a-.../occupation \
  -H 'Content-Type: application/json' \
  -d '{"occupy_id":"instance-uuid","mode":"shared","position":"localhost"}'
```

### DELETE /api/cr/:id/occupation

**场景**：释放占用。

**请求**：

```json
{ "occupy_id": "instance-uuid", "mode": "shared" }
```

```bash
curl -X DELETE http://localhost:8080/api/cr/4f9e2c1a-.../occupation \
  -H 'Content-Type: application/json' \
  -d '{"occupy_id":"instance-uuid","mode":"shared"}'
```

---

## 11. 发现与团队

跨节点发现、组队、选举、广播心跳。多机器人协作场景使用。

### GET /api/discovery

**场景**：本框架自身的 IPv4 列表（多网卡环境用来挑可达地址）。

**响应 200**：`{"ipv4":["127.0.0.1","192.168.1.5"]}`

```bash
curl http://localhost:8080/api/discovery
```

### POST /api/discovery

**场景**：按 framework_id 查另一个框架的 IPv4。

**请求**：`{"id":"framework-uuid"}`

**响应 200**：`{"ipv4":["..."]}`

### GET /api/team

**场景**：列出当前框架已加入的所有队伍。

```bash
curl http://localhost:8080/api/team
```

### POST /api/team/join

**场景**：加入一个队伍（需有效 JWT）。

**请求**：`{"teamID":"team-uuid","jwt":"<token>"}`

**响应 200**：`Successfully joined team` 或 `Already in team` (text/plain)。

```bash
curl -X POST http://localhost:8080/api/team/join \
  -H 'Content-Type: application/json' \
  -d '{"teamID":"team-uuid","jwt":"..."}'
```

**规范**：JWT 校验失败返回 error；幂等（重复加入返回 already）。

### POST /api/team/leave

**场景**：退出指定队伍。

**请求**：`{"teamID":"team-uuid"}`

**响应 200**：`Successfully left team`

```bash
curl -X POST http://localhost:8080/api/team/leave \
  -H 'Content-Type: application/json' \
  -d '{"teamID":"team-uuid"}'
```

### GET /api/team/masters

**场景**：列出已知的 master 框架（选举结果）。

### GET /api/team/peers

**场景**：列出同队伍其他节点的运行信息（IP / port / last_updated）。

### POST /api/team-heartbeat

**场景**：**框架间内部使用**，接收队伍其他成员发来的心跳。普通客户端不调。

### POST /api/team/election-msg

**场景**：**框架间内部使用**，选举消息接收。schema 待补。

### POST /api/findAbility

**场景**：在团队内全网搜索某能力的可用实例（用于跨节点编排）。schema 待补。

---

## 12. 控制器心跳

### POST /api/controller-heartbeat

**场景**：**Controller 子进程内部使用**，定期 ping 框架。普通客户端不调。

**请求**（JSON 或 CBOR）：

```json
{ "controllerInstanceId": "controller-...", "...": "..." }
```

**响应 200**：无 body。

---

## 13. 服务 CR（实验性）

Service 类型 CR 的 CRUD（与能力 CR 区分；能力 CR 是只读，Service CR 仍允许运行时增删）。

### GET /api/service-cr

列出所有服务 CR。

### GET /api/service-cr/:id

按 id 查询单个服务 CR；不存在返回 404。

### POST /api/service-cr

**场景**：创建服务 CR。

**请求**：完整 Service CR JSON（如未填 id 由框架生成）。

**响应 201**：`{"id":"uuid","status":"created"}`

```bash
curl -X POST http://localhost:8080/api/service-cr \
  -H 'Content-Type: application/json' \
  -d @service.cr.json
```

### DELETE /api/service-cr/:id

删除服务 CR。响应 200：`{"status":"deleted"}`。

---

## 14. 内部测试 API

仅用于框架自检 / dev box 调试，**生产环境不应调用**，未来版本可能下线或加访问控制。

### POST /api/internal/test-message

**场景**：往内部消息总线塞一条测试消息（同步等回包或异步发后即忘）。

**请求**：

```json
{
  "source": "TestClient",
  "destination": "TargetModule",
  "operation": "test_op",
  "payload": "{}",
  "synchronous": true
}
```

**响应 200**：同步则返回目标响应；异步则 `OK`。

### POST /api/internal/test-download-package

**场景**：触发一次包下载流程（不真正部署），用于验证 mirror 配置。

**请求**：`{"package":"...","version":"..."}`

**响应 200**：`OK` (text/plain)

---

## Endpoint 一览表

| Method              | Path                                     | 分组         | 一句话                           |
| ------------------- | ---------------------------------------- | ------------ | -------------------------------- |
| GET                 | `/api/hello`                             | Health       | 探活                             |
| GET                 | `/api/config`                            | Health       | 取全局配置                       |
| GET                 | `/api/log`                               | Health       | 取日志末尾 N 行                  |
| GET                 | `/api/manifest`                          | Manifest/CRD | 列出能力清单摘要                 |
| GET                 | `/api/manifest/:name/:version`           | Manifest/CRD | 单个能力完整清单                 |
| GET                 | `/api/crd`                               | Manifest/CRD | 列出 CRD（可过滤）               |
| GET                 | `/api/crd/:name/:version`                | Manifest/CRD | 单个 CRD                         |
| GET                 | `/api/builtin-crd/ability`               | Manifest/CRD | 内置 AtomAbility CRD schema      |
| GET                 | `/api/builtin-crd/service`               | Manifest/CRD | 内置 Service CRD schema          |
| GET                 | `/api/cr`                                | CR 模板      | 列出 CR 模板                     |
| GET                 | `/api/cr/:id`                            | CR 模板      | 单个 CR（支持 instance_id 回落） |
| GET                 | `/api/device_crs`                        | CR 模板      | 列出设备 CR                      |
| GET                 | `/api/crs`                               | CR 模板      | 列出 YAML 文件                   |
| POST                | `/api/crs/autostart`                     | CR 模板      | 改 autoStart 字段                |
| GET                 | `/api/instance`                          | 实例         | 列出所有实例                     |
| GET                 | `/api/instance/:id`                      | 实例         | 单个实例详情                     |
| POST                | `/api/instance`                          | 实例         | 从 CR 派生并启动                 |
| DELETE              | `/api/instance/:id`                      | 实例         | 终止并销毁                       |
| POST                | `/api/lifecycle-request`                 | 生命周期     | 驱动状态转移                     |
| POST                | `/api/ability-heartbeat`                 | 生命周期     | SDK 上报心跳                     |
| GET                 | `/api/ability-heartbeat`                 | 生命周期     | 列出所有心跳                     |
| GET                 | `/api/ability-heartbeat/:id`             | 生命周期     | 单实例心跳                       |
| GET/POST/PUT/DELETE | `/api/ability/:id/:subpath`              | 能力代理     | 透传到 abilityPort               |
| GET                 | `/api/task`                              | 任务         | 列出所有 task                    |
| GET                 | `/api/task/:id`                          | 任务         | 查 task 状态                     |
| POST                | `/api/task`                              | 任务         | 提交自定义 task                  |
| POST                | `/api/package`                           | 包           | 上架包                           |
| GET                 | `/api/package`                           | 包           | 列出包（待实现）                 |
| DELETE              | `/api/package/:name/:version`            | 包           | 下架包                           |
| GET                 | `/api/skill`                             | Skill        | 列出 skill 元数据                |
| GET                 | `/api/skill/:package/:version/:filename` | Skill        | 单个 skill 原文                  |
| GET                 | `/api/cr/:id/occupation`                 | 资源占用     | 查占用                           |
| POST                | `/api/cr/:id/occupation`                 | 资源占用     | 申请占用                         |
| DELETE              | `/api/cr/:id/occupation`                 | 资源占用     | 释放占用                         |
| GET                 | `/api/discovery`                         | 发现         | 本机 IPv4                        |
| POST                | `/api/discovery`                         | 发现         | 按 ID 查远端 IPv4                |
| GET                 | `/api/team`                              | 团队         | 已加入的队伍                     |
| POST                | `/api/team/join`                         | 团队         | 加入                             |
| POST                | `/api/team/leave`                        | 团队         | 退出                             |
| GET                 | `/api/team/masters`                      | 团队         | 已知 master                      |
| GET                 | `/api/team/peers`                        | 团队         | 同队伍成员                       |
| POST                | `/api/team-heartbeat`                    | 团队         | 框架间心跳                       |
| POST                | `/api/team/election-msg`                 | 团队         | 选举消息                         |
| POST                | `/api/findAbility`                       | 团队         | 跨节点搜能力                     |
| POST                | `/api/controller-heartbeat`              | 控制器       | controller 心跳                  |
| GET                 | `/api/service-cr`                        | Service CR   | 列出                             |
| GET                 | `/api/service-cr/:id`                    | Service CR   | 单个                             |
| POST                | `/api/service-cr`                        | Service CR   | 创建                             |
| DELETE              | `/api/service-cr/:id`                    | Service CR   | 删除                             |
| POST                | `/api/internal/test-message`             | 内部测试     | 测试消息总线                     |
| POST                | `/api/internal/test-download-package`    | 内部测试     | 测试包下载                       |

合计 47 个公开 endpoint（不含 deprecated 的 `POST/DELETE /api/cr` 与旧版 `/api/resourcemgr/*`）。
