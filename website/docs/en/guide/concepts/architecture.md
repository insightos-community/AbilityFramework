# Overview

AbilityFramework consists of several decoupled manager modules. The modules collaborate through a **message bus** (synchronous/asynchronous messages) and the **libuv event loop** (timers, asynchronous tasks), and each exposes REST endpoints to the HTTP server. Each file in this directory introduces a module's responsibilities, interfaces, and configuration.

## Module overview

| Module | Message bus name | Docs | One-line responsibility |
|---|---|---|---|
| ResourceManager | `ResourceMgr` | [resource_manager.md](/en/guide/concepts/architecture) | Registry of ability/device resources, runtime instances, the package repository, and ownership relations |
| SubprocessManager | `SubprocessMgr` | [subprocess_manager.md](/en/guide/concepts/architecture) | Starting, handle maintenance, and exit reaping of ability and controller processes |
| LifecycleManager | `LifecycleMgr` | [lifecycle_manager.md](/en/guide/concepts/architecture) | Ability heartbeat maintenance and the lifecycle state machine |
| TaskManager | `TaskMgr` | [task_manager.md](/en/guide/concepts/architecture) | The asynchronous task scheduling engine based on C++20 coroutines |
| TaskStatusManager | `TaskStatusMgr` | [task_status_manager.md](/en/guide/concepts/architecture) | Persistence and history queries for task execution status |
| ControllerManager | `ControllerMgr` | [controller_manager.md](/en/guide/concepts/architecture) | Automatic discovery and launch of ability controllers |
| DiscoveryManager | none | [discovery_manager.md](/en/guide/concepts/architecture) | Multi-node networking, team management, and leader election |
| AbilityAlertManager | `AbilityAlertMgr` | [ability_alert_manager.md](/en/guide/concepts/architecture) | Collection, persistence, and linked handling of ability runtime alerts |

## Module startup order

`src/main.cpp` constructs and registers the modules in this order:

```cpp
message_bus::add_modules(
    task_mgr, lifecycle_mgr, resource_mgr,
    subproccess_mgr, task_status_mgr, controller_mgr
);
```

DiscoveryManager and AbilityAlertManager are not registered on the message bus; they are constructed directly and bind their HTTP endpoints.

## Full module collaboration

A typical cross-module chain for "create and start an ability instance":

### Global architecture C4 diagram

```mermaid
C4Context
    title AbilityFramework module architecture

    Person(user, "Caller", "Studio / MCP Server / Integrator")
        System(http, "HTTP Server", "REST API + WebUI")

    System_Boundary(fwk, "AbilityFramework") {
        System(res, "ResourceManager", "Resource registry / CR / CRD / Instance")
        System(sub, "SubprocessManager", "Process start / exit reap")
        System(lc, "LifecycleManager", "Heartbeat / lifecycle state machine")
        System(tm, "TaskManager", "Async task scheduling engine")
        System(tsm, "TaskStatusManager", "Task status persistence")
        System(cm, "ControllerManager", "Controller auto-launch")
        System(dm, "DiscoveryManager", "Networking / teams / election")
        System(am, "AbilityAlertManager", "Alert collection / linked handling")
    }

    Boundary(c, "") {
        System_Ext(ability, "Ability process", "ability / controller")
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

    Rel(lc, res, "query CR / validate heartbeat")
    Rel(lc, sub, "start_process")
    Rel(res, lc, "lifecycle_request")
    Rel(lc, tm, "submit async task")
    Rel(cm, lc, "query running abilities")
    Rel(cm, sub, "start_controller")
    Rel(am, tsm, "query task status")
    Rel(am, lc, "query ability heartbeat")

    Rel(sub, ability, "spawn / zenoh")
    Rel(ability, http, "heartbeat / alert report")
    Rel(res, db, "CR/CRD/Instance")
    Rel(am, db, "Alert persistence")
    Rel(tsm, db, "TaskStatus persistence")
    
    UpdateLayoutConfig($c4ShapeInRow="4", $c4BoundaryInRow="1")
```

### Create and start an ability instance — sequence diagram

```mermaid
sequenceDiagram
    participant U as Caller
    participant API as HTTP Server
    participant RM as ResourceManager
    participant LC as LifecycleManager
    participant TM as TaskManager
    participant SP as SubprocessManager
    participant AB as Ability process
    participant CM as ControllerManager

    U->>API: POST /api/instance
    API->>RM: create AbilityInstance
    RM->>LC: lifecycle_request(start)
    LC->>RM: find_ability_instance/id
    RM-->>LC: AbilityCR
    LC->>RM: find_ability_storage_info/id
    RM-->>LC: executable path
    LC->>TM: submit sequence task
    TM->>SP: start_process (with exit callback)
    SP->>AB: spawn process

    loop periodic report
        AB->>API: POST /api/ability-heartbeat
        API->>LC: on_heartbeat
        LC->>RM: HeartbeatEvent validation
        RM-->>LC: accepted
        LC-->>API: 200 OK
    end

    par periodic scan
        CM->>LC: running_abilities
        LC-->>CM: ability list
        CM->>SP: start_controller
    end
```

Related references:

- [HTTP API reference](/en/api/http-api)
- [Configuration file reference](/en/guide/configuration/config-file)
- [CLI command reference](/en/guide/cli)
