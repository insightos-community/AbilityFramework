# Python SDK

> 仓库名：`ability-py-sdk`

Python SDK 提供 `AbilityInterface` 基类和 `ServiceInterface` 基类，用于编写能力进程。

## AbilityInterface

能力开发者继承 `AbilityInterface` 并实现以下回调：

```python
class ImplAbility(ability_py.AbilityInterface):
    def on_start(self):
        # 初始化资源（加载模型、连接设备等）
        pass

    def on_connect(self):
        # 绑定端口，启动 IPC 服务
        self.ability_port = ability_py.get_free_port()

    def on_disconnect(self):
        # 停止 IPC 服务
        pass

    def on_terminate(self):
        # 释放资源
        pass

    def get_ability_port(self) -> int:
        return self.ability_port
```

## ServiceInterface

Service 类型能力的基类（v3 新增）：

```python
class ServiceInterface:
    def on_start(self):
        pass

    def on_stop(self):
        pass

    def health_check(self) -> bool:
        return True

    def get_service_port(self) -> int:
        return 0
```

## 心跳与生命周期

SDK 内部自动处理：

- 每 5 秒向框架发送 `POST /api/ability-heartbeat`
- 连续收到 `410 Gone` 后自毁退出（框架不再认识该实例）
- 日志级别通过 `LOG_LEVEL` 环境变量与主进程保持一致

## 脚手架工具

配合脚手架工具，开发者只需关注 `ability.py` 的 `on_start()` 和 `task.py` 的 `execute()`。

详见 [架构演进设计 - Phase 1.5](/guide/release-highlights/architecture-evolution#phase-15能力工程脚手架工具)。

## 相关参考

- [能力实例](/guide/release-highlights/instance) — 实例生命周期状态机
- [Skill 文档系统](/guide/release-highlights/skill) — 为能力编写 AI Agent 说明
- [架构演进设计](/guide/release-highlights/architecture-evolution)
