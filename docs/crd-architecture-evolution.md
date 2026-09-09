# AbilityFramework CRD/CR 架构演进设计

## 背景

当前 CRD 文件由每个能力包各自携带（`ability.crd.yaml` 或 `crds/*.crd.yaml`），混淆了框架职责与能力开发者职责。参照 k8s CRD/CR 模式：

- **CRD** 应为框架内置的资源类型定义（类似 k8s operator 提供的 CRD），定义资源的通用结构
- **CR** 才是能力开发者编写的实例声明，描述具体能力的 spec、接口和配置

### 当前问题

1. **职责混淆** — `AbilityCRD` 结构体中混合了框架级字段（ownership/bind modes、x-intentable、x-ref、FwkDetail）和能力特有字段（provides、types、rpcMethods、tasks、config、debugOption、openAPIV3Schema）
2. **冗余携带** — 每个能力包都携带一份 CRD，但 CRD 的框架级 schema 对所有能力是相同的
3. **工具链概念错误** — openapi-tool 的 `generate-crd-from-openapi.py` 将输出命名为"CRD"，但其实际目的是从 OpenAPI 定义生成符合能力规范的 CR 文件；工具混淆了 CRD（框架规范）与 CR（能力实例声明）的概念
4. **缺少 Service 类型** — 框架仅支持按需调用的 Ability，不支持常驻运行的 Service

### 相关工程

| 工程 | 作用 |
|------|------|
| **abilityframework** | 核心框架，管理能力部署、运行、监控 |
| **openapi-tool** | 通过 OpenAPI YAML 定义能力接口，生成符合 CRD 规范的 CR 文件（`generate-crd-from-openapi.py`，当前误命名为"生成 CRD"），支持 `x-insightos`/`x-ability-config`/`x-tasks`/`x-depends` 等扩展 |
| **ability-py-sdk** | Python 能力开发 SDK，提供 `AbilityInterface` 基类（on_start/on_connect/on_disconnect/on_terminate）、心跳管理、Flask IPC |
| **ability-sdk** | C++ 能力开发 SDK，提供 AbilityStub、AbilityConfig、TaskHelper 等 |

---

## Phase 1：CRD 内置化，引入 Manifest

### 核心思路

将 CRD 从能力包中抽离为框架内置定义。原 CRD 中的能力特有字段迁移到新文件 `ability.manifest.yaml`（能力清单）。

### 职责划分

```
框架维护（CRD）                     能力开发者编写（Manifest + CR）
┌─────────────────────┐           ┌─────────────────────────┐
│ ability.crd          │           │ ability.manifest.yaml    │
│ - CR 结构 schema     │           │ - provides              │
│ - kind 枚举          │           │ - types, rpcMethods     │
│ - spec 通用字段      │           │ - tasks                 │
│ - metadata 规范      │           │ - config/status schema  │
│ - x-intentable 语义  │           │ - depends               │
│ - x-ref 语义         │           │ - constants             │
└─────────────────────┘           └─────────────────────────┘
                                  ┌─────────────────────────┐
                                  │ my-ability.cr.yaml       │
                                  │ - kind: AtomAbility      │
                                  │ - metadata.name          │
                                  │ - spec.package/version   │
                                  │ - spec.config (具体值)   │
                                  │ - spec.devices           │
                                  └─────────────────────────┘
```

### CRD（框架内置，唯一一份）

内嵌于框架二进制或从 `$ABILITY_FRAMEWORK_HOME/schema/ability.crd.yaml` 加载：

```yaml
apiVersion: framework/v1
kind: CustomResourceDefinition
metadata:
  name: ability.crd
spec:
  names:
    kind: Ability           # 涵盖 AtomAbility, ComposeAbility, AbstractAbility
  schema:
    openAPIV3Schema:
      type: object
      required: [kind, metadata, spec]
      properties:
        kind:
          type: string
          enum: [AtomAbility, ComposeAbility, AbstractAbility]
        metadata:
          type: object
          required: [name]
          properties:
            name: { type: string }
            labels: { type: object, additionalProperties: { type: string } }
            annotations: { type: object, additionalProperties: { type: string } }
        spec:
          type: object
          required: [package, version, abilityName]
          properties:
            package: { type: string }
            version: { type: string }
            abilityName: { type: string }
            position: { type: string, default: "localhost" }
            autoStart: { type: boolean }
            priority: { type: integer }
            activityCondition: {}
            config: {}              # 具体 schema 由 manifest 中的 schema.config 定义
            debugOption: {}         # 具体 schema 由 manifest 中的 schema.debugOption 定义
            subabilities: { type: array }
            devices: { type: array }
            models: { type: array }
```

### Ability Manifest（能力清单，包内携带）

由能力开发者编写（可通过 openapi-tool 生成），随包发布：

```yaml
abilityName: MyAbility.org
kind: AtomAbility
provides:                         # 能力对外提供的服务描述
  - name: ObjectDetection
    description: "检测图像中的物体"
types: { ... }                    # 自定义类型定义
rpcMethods: { ... }               # RPC 方法签名
tasks:                            # 任务定义（对应 openapi-tool 的 x-tasks）
  - taskType: 0
    summary: "执行一次检测"
    params:
      - name: timeout
        type: int
        optional: true
constants: { ... }                # 提供给父能力的常量
schema:
  config:                         # 验证 CR spec.config 的 OpenAPI V3 Schema
    openAPIV3Schema:
      type: object
      properties:
        threshold: { type: number, default: 0.5 }
  status:                         # 验证运行时 status 的 Schema
    openAPIV3Schema: { ... }
  debugOption:                    # 验证 CR spec.debugOption 的 Schema
    openAPIV3Schema:
      type: object
      properties:
        logPicture: { type: boolean, default: false }
depends:                          # 依赖声明
  abilities:
    - abilityName: Sub.Ability
      ownership: unique
      bind: local
  devices:
    - deviceName: Camera
      ownership: shared
  models:                         # 模型依赖（对应 openapi-tool 的 x-depends.models）
    - spec:
        name: yolov5
        architecture: object_detection
        framework: pytorch
```

### 包结构变更

```
packages/<name>/<version>/
  package.yaml               # 包清单（name, version, arch）
  ability.manifest.yaml      # 能力清单（NEW，替代原 CRD）
  bin/
    ability                  # 可执行文件
```

### 验证流程变为两级

1. **框架级验证** — CR 整体结构符合内置 ability.crd 的 schema
2. **能力级验证** — CR 的 config/debugOption/status 符合 manifest 中的 schema（x-intentable/x-ref 展开在此阶段执行）

### openapi-tool 适配

`generate-crd-from-openapi.py` 需要修正概念并调整输出：

- **当前（概念错误）**：从 OpenAPI YAML 生成 `*.crd.yaml`，但实际生成的是 CR 而非 CRD
- **Phase 1 后（概念修正）**：
  - 脚本重命名为 `generate-cr-from-openapi.py`，明确其生成的是 CR 文件
  - 输出目标改为 `*.cr.yaml`（符合 ability.crd 规范的 CR）
  - 同时输出 `ability.manifest.yaml`（能力清单，包含接口描述、schema、依赖等）
- 核心转换逻辑不变（x-insightos → metadata、x-ability-config → spec.config、x-tasks → manifest.tasks、x-depends → manifest.depends），调整输出格式和字段归属

### ability-py-sdk 适配

SDK 本身无需大改。当前 SDK 通过 `/api/cr/{uuid}` 获取 CR 信息来初始化能力，这个流程不变。影响点：

- 框架返回的 CR 结构不变（CR 字段未改）
- 若 SDK 需读取能力接口描述（manifest 内容），新增 `/api/manifest/{abilityName}/{version}` 端点

### 关键代码变更

| 文件 | 变更 |
|------|------|
| 新增 `include/resourcemgr/ability_manifest.hpp` | AbilityManifest 结构体（从 AbilityCRD::Spec 提取） |
| 新增 `src/resourcemgr/ability_manifest.cpp` | JSON 序列化、从旧 CRD 转换的兼容逻辑 |
| `include/resourcemgr/ability_crd.hpp` | 精简，仅保留框架级 schema 定义 |
| `include/resourcemgr/ability_pkg.hpp` | AbilityPackage 增加 manifests 字段 |
| `src/resourcemgr/resource_mgr.cpp` | read_package() 改为读取 manifest；框架 CRD 启动时加载一次 |
| `src/resourcemgr/json_schema_utils.cpp` | 移除 get_ability_crd_path()；验证改为两级（框架 CRD + manifest） |
| `src/resourcemgr/resource_mgr_http_apis.cpp` | 适配新验证路径；新增 manifest 查询端点 |
| `openapi-tool/generate-crd-from-openapi.py` | 重命名为 `generate-cr-from-openapi.py`，输出 CR + manifest |

### 向后兼容

- 若包中存在旧格式 `ability.crd.yaml` 或 `crds/*.crd.yaml`，自动提取 manifest 字段生成内存中的 AbilityManifest
- 输出 deprecation 警告日志
- 旧包无需改动即可继续使用

---

## Phase 1.5：能力工程脚手架工具

### 核心思路

联合 openapi-tool 和 ability-py-sdk，提供完整的能力开发工具链：

1. **openapi-tool** 从 OpenAPI 定义生成 CR 文件（能力声明）
2. **脚手架工具** 读取 CR 中的 tasks 定义，结合 SDK 模板，自动生成能力工程代码

开发者只需：定义 OpenAPI 接口 → 生成 CR → 生成工程 → **实现 on_start() 初始化 + 各 task 函数体**。

### 工具链流程

```
  OpenAPI YAML                   CR YAML                     能力工程
  (接口定义)                     (能力声明)                   (可运行代码)

  x-tasks:                       spec:                       task.py:
    - taskType: 0        ──>       tasks:               ──>    class DetectTask(TaskInterface):
      summary: 检测                  - taskType: 0                def execute(self, input: dict) -> dict:
      params:                          params:                       timeout = input.get("timeout", 0)
        - name: timeout                  ...                          # TODO: 实现检测逻辑
          type: int                                                   raise NotImplementedError
      returns:
        - name: result
          type: array

  ┌─────────────┐            ┌─────────────┐            ┌─────────────────────┐
  │ openapi-tool │    ──>    │ CR + Manifest│    ──>    │ scaffold-tool        │
  │ (定义接口)   │            │ (生成声明)   │            │ (生成工程代码)       │
  └─────────────┘            └─────────────┘            └─────────────────────┘
```

### 脚手架生成的工程结构

以 CR 中定义了 3 个 task（taskType: 0=Detect, 1=Track, 2=Classify）为例：

```
my-ability/
  main.py                          # 入口，自动生成，通常不需要改
  task.py                          # task 实现，开发者填充函数体
  service/
    __init__.py                    # 导出 ImplAbility, task_manager
    ability.py                     # AbilityInterface 实现，开发者填充 on_start
    server.py                      # Flask API server（从 SDK 模板复制）
    interface.py                   # TaskInterface 基类（从 SDK 模板复制）
    task_manager.py                # TaskManager（从 SDK 模板复制）
  ability.cr.yaml                  # 由 openapi-tool 生成的 CR
  ability.manifest.yaml            # 由 openapi-tool 生成的 Manifest
```

### 生成的 task.py 示例

假设 CR 定义了：

```yaml
spec:
  tasks:
    - taskType: 0
      summary: "单次识别任务"
      params:
        - name: timeout
          type: int
          optional: true
        - name: image_url
          type: string
      returns:
        - name: objects
          type: array
        - name: confidence
          type: number
```

脚手架生成：

```python
from service import TaskInterface


class RecognizeTask(TaskInterface):
    """单次识别任务 (taskType: 0)"""

    def execute(self, input_data: dict) -> dict:
        """
        Args:
            input_data: {
                "timeout": int (optional),
                "image_url": str,
            }

        Returns:
            {
                "objects": list,
                "confidence": float,
            }
        """
        timeout = input_data.get("timeout", 0)
        image_url = input_data.get("image_url", "")

        # TODO: 实现识别逻辑
        raise NotImplementedError("请实现 RecognizeTask.execute()")
```

### 生成的 ability.py 示例

```python
import ability_py
import threading
from .server import app
from werkzeug.serving import make_server


class ServerThread(threading.Thread):
    def __init__(self, host="0.0.0.0", port=8080):
        super().__init__()
        self.server = make_server(host, port, app)

    def run(self):
        self.server.serve_forever()

    def shutdown(self):
        self.server.shutdown()


class ImplAbility(ability_py.AbilityInterface):
    def __init__(self):
        self.ability_port = 0
        self.st = None

    def on_start(self):
        # TODO: 初始化资源（如加载模型、连接设备等）
        pass

    def on_connect(self):
        if self.ability_port:
            return
        self.ability_port = ability_py.get_free_port()
        self.st = ServerThread(host="localhost", port=self.ability_port)
        self.st.start()

    def on_disconnect(self):
        if self.ability_port != 0:
            self.st.shutdown()
            self.ability_port = 0

    def on_terminate(self):
        # TODO: 释放资源
        pass

    def get_ability_port(self) -> int:
        return self.ability_port
```

### 生成的 main.py 示例

```python
from service import ImplAbility, task_manager
import ability_py
from task import RecognizeTask  # 自动根据 CR tasks 生成导入


# 注册 task（自动根据 CR 中 taskType 映射生成）
tasks = {
    0: RecognizeTask(),
}

task_manager.register_tasks(tasks)

if __name__ == "__main__":
    ability = ImplAbility()
    service = ability_py.AbilityService()
    service.run(ability)
```

### 脚手架工具命令

```bash
# 从 CR 生成能力工程
python scaffold.py --cr ability.cr.yaml --output ./my-ability

# 从 OpenAPI 一步到位：生成 CR + 工程
python scaffold.py --openapi MyAbility.openapi.yaml --output ./my-ability
```

### 关键实现

脚手架工具（`scaffold.py`）读取 CR/Manifest 中的 tasks 定义：

1. 为每个 taskType 生成一个 Task 类（类名从 summary 或 taskType 派生）
2. execute() 方法中预填充参数解包代码（`input_data.get("param_name", default)`）
3. 返回值 docstring 中列出 returns 字段
4. main.py 中自动注册所有 task
5. ability.py 提供标准的生命周期骨架，on_start/on_terminate 留 TODO
6. service/ 目录从 SDK 模板复制（server.py, interface.py, task_manager.py）

### 开发者只需关注

1. **`ability.py` 中的 `on_start()`** — 初始化逻辑（加载模型、连接硬件等）
2. **`task.py` 中各 Task 的 `execute()`** — 业务实现（检测、规划、控制等）
3. **`ability.py` 中的 `on_terminate()`**（可选）— 资源释放

其余代码（main.py 入口、Flask server、TaskManager、心跳、生命周期响应）全部由脚手架 + SDK 提供。

---

## Phase 2：新增 Service 资源类型

### 核心思路

Ability 是按需调用的，Service 是常驻运行的。框架新增内置 `service.crd`，与 `ability.crd` 并列。

### Service 与 Ability 的区别

| 维度 | Ability | Service |
|------|---------|---------|
| 用途 | 按需执行任务（如目标检测、路径规划） | 常驻提供服务（如视频流、传感器采集） |
| 生命周期 | Inactive→Init→Standby→Running→Suspend→Terminated | Stopped→Starting→Running→Failed→Restarting |
| 退出行为 | 正常退出，通知父能力 | 按 restartPolicy 自动重启 |
| 重启策略 | 无 | always / on-failure / never |
| 健康检查 | 仅心跳超时（1分钟被动检测） | 主动探活：HTTP liveness + readiness |
| SDK 接口 | on_start/on_connect/on_disconnect/on_terminate | on_start/on_stop + health endpoint |

### Service CRD（框架内置）

```yaml
apiVersion: framework/v1
kind: CustomResourceDefinition
metadata:
  name: service.crd
spec:
  names:
    kind: Service
  schema:
    openAPIV3Schema:
      type: object
      required: [kind, metadata, spec]
      properties:
        kind:
          type: string
          enum: [Service]
        metadata:
          type: object
          required: [name]
          properties:
            name: { type: string }
            labels: { type: object, additionalProperties: { type: string } }
            annotations: { type: object, additionalProperties: { type: string } }
        spec:
          type: object
          required: [package, version, serviceName]
          properties:
            package: { type: string }
            version: { type: string }
            serviceName: { type: string }
            position: { type: string, default: "localhost" }
            config: {}
            restartPolicy:
              type: string
              enum: [always, on-failure, never]
              default: always
            maxRestarts: { type: integer, default: 5 }
            restartBackoffSeconds: { type: integer, default: 10 }
            healthCheck:
              type: object
              properties:
                liveness:
                  type: object
                  properties:
                    type: { type: string, enum: [http, process] }
                    path: { type: string }
                    port: { type: integer }
                    intervalSeconds: { type: integer, default: 10 }
                    failureThreshold: { type: integer, default: 3 }
                readiness:
                  type: object
                  properties:
                    type: { type: string, enum: [http] }
                    path: { type: string }
                    port: { type: integer }
                    intervalSeconds: { type: integer, default: 5 }
            devices: { type: array }
            models: { type: array }
```

### Service CR 示例

```yaml
kind: Service
metadata:
  name: camera-stream
spec:
  package: com.robot.camera
  version: 1.0.0
  serviceName: CameraStream
  position: localhost
  restartPolicy: always
  maxRestarts: 5
  restartBackoffSeconds: 10
  healthCheck:
    liveness:
      type: http
      path: /health
      port: 9090
      intervalSeconds: 10
      failureThreshold: 3
  config:
    resolution: "1080p"
    fps: 30
```

### Service 包结构

```
packages/<name>/<version>/
  package.yaml                # kind: service（新增 kind 字段区分）
  service.manifest.yaml       # 服务清单
  bin/
    service                   # 可执行文件
```

`package.yaml` 新增 `kind` 字段，缺省值为 `ability` 保持向后兼容。

### 重启策略实现

```
进程退出 -> on_service_exit() 回调
  |-- restartPolicy == never -> 标记 Stopped，不重启
  |-- restartPolicy == on-failure && exit_code == 0 -> 标记 Stopped，不重启
  +-- 其他 -> 检查 restartCount < maxRestarts
      |-- 是 -> 等待 backoff * 2^restartCount 秒后重启，标记 Restarting
      +-- 否 -> 标记 Failed，不再重启
```

### ability-py-sdk 扩展

新增 `ServiceInterface` 基类：

```python
class ServiceInterface:
    def on_start(self):
        """初始化并启动服务"""
        pass

    def on_stop(self):
        """停止服务"""
        pass

    def health_check(self) -> bool:
        """返回服务健康状态，框架定期调用"""
        return True

    def get_service_port(self) -> int:
        """返回服务监听端口"""
        return 0
```

### 关键代码变更

| 文件 | 变更 |
|------|------|
| 新增 `include/resourcemgr/service_cr.hpp` | ServiceCR, ServiceSpec 结构体 |
| 新增 `include/lifecyclemgr/service_state.hpp` | ServiceState 枚举（Stopped/Starting/Running/Failed/Restarting） |
| 新增 `src/lifecyclemgr/service_lifecycle.cpp` | 重启策略实现、健康检查探活逻辑 |
| `src/lifecyclemgr/lifecycle_mgr.cpp` | on_service_exit() 触发重启决策 |
| `src/resourcemgr/resource_mgr.cpp` | ServiceCR 增删查、SQL_CREATE_SERVICE_CR_BASIC 表 |
| `src/resourcemgr/resource_mgr_http_apis.cpp` | 新增 /api/service-cr、/api/service-heartbeat 等端点 |
| `src/main.cpp` | 注册健康检查定时器 |
| `ability-py-sdk/src/ability_py/` | 新增 ServiceInterface、ServiceService |

### 向后兼容

Phase 2 纯增量，不影响现有 Ability 的任何行为。

---

## Phase 3：泛化与统一

### 3.1 资源类型注册表

抽象 `ResourceTypeHandler` 接口：

```cpp
class ResourceTypeHandler {
public:
    virtual ~ResourceTypeHandler() = default;
    virtual void validate_cr(const json& cr) = 0;
    virtual void on_start(const uuid& id) = 0;
    virtual void on_stop(const uuid& id) = 0;
    virtual void on_exit(const uuid& id, int exit_code) = 0;
    virtual void health_check(const uuid& id) = 0;
};
```

Ability 和 Service 各实现一个 handler，通过 kind 查找。未来新增资源类型只需实现 handler + 注册 CRD。

### 3.2 统一包格式 v3

```
packages/<name>/<version>/
  package.yaml          # name, version, arch, kind
  manifest.yaml         # 统一清单（通过 kind 区分 ability/service）
  bin/
    main                # 统一入口
```

### 3.3 Ability 可选 keepAlive

复用 Phase 2 的重启基础设施，允许 Ability 声明 keepAlive：

```yaml
kind: AtomAbility
spec:
  keepAlive:
    enabled: true
    maxRestarts: 3
    restartBackoffSeconds: 15
```

### 3.4 旧格式移除时间线

- Phase 3a: 包含旧 CRD 的包输出 WARNING
- Phase 3b（下一个大版本）: 移除从包中读取 CRD 的兼容代码
- Phase 3c: 移除 v1 包格式（crds/ 目录）支持

---

## 实施顺序

```
Phase 1 (CRD 内置化):
  1. 创建 AbilityManifest 结构体及序列化
  2. 框架 CRD 作为内嵌资源加载
  3. read_package() 支持读取 ability.manifest.yaml
  4. 旧 CRD -> Manifest 兼容桥
  5. 重构验证流程（json_schema_utils.cpp）
  6. 更新 HTTP API
  7. 更新 DB 表
  8. openapi-tool 重命名脚本，修正概念，输出 CR + manifest
  9. 集成测试

Phase 1.5 (能力工程脚手架):
  1. scaffold.py 核心逻辑：读取 CR/Manifest 中的 tasks 定义
  2. Task 类代码生成（参数解包、返回值注释、TODO 占位）
  3. ability.py 生命周期骨架生成
  4. main.py 入口生成（自动注册 task）
  5. service/ 模板目录从 SDK 复制
  6. --openapi 模式：集成 openapi-tool 一步到位
  7. 端到端测试：OpenAPI → CR → 工程 → 框架加载运行

Phase 2 (Service 资源类型):
  1. ServiceCR/ServiceSpec 结构体
  2. Service CRD 内嵌
  3. ServiceState 枚举及生命周期逻辑
  4. 重启策略 + 健康检查
  5. Service HTTP API
  6. Service DB 表
  7. package.yaml kind 字段
  8. ability-py-sdk ServiceInterface
  9. 集成测试

Phase 3 (泛化):
  1. ResourceTypeHandler 抽象
  2. 统一包格式
  3. Ability keepAlive
  4. 旧格式废弃路径
```

## 验证方式

### Phase 1
1. 新格式包（含 ability.manifest.yaml）能被正确加载并校验 CR
2. 旧格式包（含 ability.crd.yaml）通过兼容桥正确转换
3. 新旧验证路径结果一致
4. `GET /api/crd` 返回框架级 CRD
5. openapi-tool（重命名后）能正确输出 CR + manifest 格式

### Phase 1.5
1. 从示例 CR 生成工程，确认目录结构和文件完整
2. 生成的 task.py 中参数解包代码与 CR tasks 定义一致
3. 填充 task execute() 后，工程可被框架正常加载运行
4. --openapi 模式端到端：OpenAPI YAML → 生成工程 → 框架启动能力 → 调用 task API 成功

### Phase 2
1. 创建 Service CR，验证进程启动
2. kill 进程，按 restartPolicy 自动重启
3. 达到 maxRestarts 后标记 Failed
4. 健康检查探活失败触发重启
5. restartPolicy: never 验证退出不重启
6. ability-py-sdk ServiceInterface 能正常工作
