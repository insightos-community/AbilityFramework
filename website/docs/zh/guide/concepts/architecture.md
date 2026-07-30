# 总览

AbilityFramework 由若干个解耦的管理器模块组成，模块间通过**消息总线**（同步/异步消息）与 **libuv 事件循环**（定时器、异步任务）协作，并各自向 HTTP 服务器暴露 REST 接口。本文档目录下每个文件介绍一个模块的职责、接口与配置。

## 模块速览

| 模块 | 消息总线名 | 文档 | 一句话职责 |
|---|---|---|---|
| ResourceManager | `ResourceMgr` | [resource_manager.md](/guide/concepts/architecture) | 能力/设备资源的注册表、运行时实例、包仓库与占用关系 |
| SubprocessManager | `SubprocessMgr` | [subprocess_manager.md](/guide/concepts/architecture) | 能力进程与控制器的启动、句柄维护与退出回收 |
| LifecycleManager | `LifecycleMgr` | [lifecycle_manager.md](/guide/concepts/architecture) | 能力心跳维护与生命周期状态机驱动 |
| TaskManager | `TaskMgr` | [task_manager.md](/guide/concepts/architecture) | 基于 C++20 协程的异步任务调度引擎 |
| TaskStatusManager | `TaskStatusMgr` | [task_status_manager.md](/guide/concepts/architecture) | 任务执行状态的持久化与历史查询 |
| ControllerManager | `ControllerMgr` | [controller_manager.md](/guide/concepts/architecture) | 能力控制器的自动发现与拉起 |
| DiscoveryManager | 无 | [discovery_manager.md](/guide/concepts/architecture) | 多节点组网、队伍管理与主从选举 |
| AbilityAlertManager | `AbilityAlertMgr` | [ability_alert_manager.md](/guide/concepts/architecture) | 能力运行时告警的采集、持久化与联动处置 |

## 模块启动顺序

`src/main.cpp` 按以下顺序构造并注册各模块：

```cpp
message_bus::add_modules(
    task_mgr, lifecycle_mgr, resource_mgr,
    subproccess_mgr, task_status_mgr, controller_mgr
);
```

DiscoveryManager 与 AbilityAlertManager 不走消息总线注册，直接构造后绑定 HTTP 接口。

## 模块协作全景

一次"创建并启动一个能力实例"的典型跨模块链路：

### 全局架构 C4 图

```mermaid
C4Context
    title AbilityFramework 模块架构

    Person(user, "上层调用方", "Studio / MCP Server / 业务集成方")
        System(http, "HTTP Server", "REST API + WebUI")

    System_Boundary(fwk, "AbilityFramework") {
        System(res, "ResourceManager", "资源注册表 / CR / CRD / 实例")
        System(sub, "SubprocessManager", "进程启动 / 退出回收")
        System(lc, "LifecycleManager", "心跳 / 生命周期状态机")
        System(tm, "TaskManager", "异步任务调度引擎")
        System(tsm, "TaskStatusManager", "任务状态持久化")
        System(cm, "ControllerManager", "控制器自动拉起")
        System(dm, "DiscoveryManager", "组网 / 队伍 / 选举")
        System(am, "AbilityAlertManager", "告警采集 / 联动处置")
    }

    Boundary(c, "") {
        System_Ext(ability, "能力进程", "ability / controller")
        System_Ext(db, "SQLite", "ability_framework.db\nability_alert.db")
    }

    Rel(user, http, "HTTP REST")
    Rel(http, res, "")
    Rel(http, lc, "")
    Rel(http, tm, "")
    Rel(http, dm, "")
    Rel(http, am, "")
    Rel(http, tsm, "")
    Rel(http, cm, "")

    Rel(lc, res, "查 CR / 校验心跳")
    Rel(lc, sub, "start_process")
    Rel(res, lc, "lifecycle_request")
    Rel(lc, tm, "提交异步任务")
    Rel(cm, lc, "查运行中能力")
    Rel(cm, sub, "start_controller")
    Rel(am, tsm, "查任务状态")
    Rel(am, lc, "查能力心跳")

    Rel(sub, ability, "spawn / zenoh")
    Rel(ability, http, "心跳 / 告警上报")
    Rel(res, db, "CR/CRD/Instance")
    Rel(am, db, "Alert 持久化")
    Rel(tsm, db, "TaskStatus 持久化")
    
    UpdateLayoutConfig($c4ShapeInRow="4", $c4BoundaryInRow="1")
```

### 创建并启动能力实例 - 时序图

```mermaid
sequenceDiagram
    participant U as 调用方
    participant API as HTTP Server
    participant RM as ResourceManager
    participant LC as LifecycleManager
    participant TM as TaskManager
    participant SP as SubprocessManager
    participant AB as 能力进程
    participant CM as ControllerManager

    U->>API: POST /api/instance
    API->>RM: 创建 AbilityInstance
    RM->>LC: lifecycle_request(start)
    LC->>RM: find_ability_instance/id
    RM-->>LC: AbilityCR
    LC->>RM: find_ability_storage_info/id
    RM-->>LC: 可执行文件路径
    LC->>TM: 提交 sequence 任务
    TM->>SP: start_process (带退出回调)
    SP->>AB: spawn 进程

    loop 周期上报
        AB->>API: POST /api/ability-heartbeat
        API->>LC: on_heartbeat
        LC->>RM: HeartbeatEvent 校验
        RM-->>LC: accepted
        LC-->>API: 200 OK
    end

    par 周期扫描
        CM->>LC: running_abilities
        LC-->>CM: 能力列表
        CM->>SP: start_controller
    end
```

相关参考：

- [HTTP API 参考](/api/http-api)
- [配置文件参考](/guide/configuration/config-file)
- [CLI 命令参考](/guide/cli)
