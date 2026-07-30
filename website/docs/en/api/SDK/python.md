# Python SDK

> Repository name: `ability-py-sdk`

The Python SDK provides the `AbilityInterface` base class and the `ServiceInterface` base class for writing ability processes.

## AbilityInterface

Ability developers extend `AbilityInterface` and implement the following callbacks:

```python
class ImplAbility(ability_py.AbilityInterface):
    def on_start(self):
        # initialize resources (load models, connect devices, etc.)
        pass

    def on_connect(self):
        # bind a port and start the IPC service
        self.ability_port = ability_py.get_free_port()

    def on_disconnect(self):
        # stop the IPC service
        pass

    def on_terminate(self):
        # release resources
        pass

    def get_ability_port(self) -> int:
        return self.ability_port
```

## ServiceInterface

The base class for Service-type abilities (new in v3):

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

## Heartbeat and lifecycle

The SDK handles this internally:

- Sends `POST /api/ability-heartbeat` to the framework every 5 seconds
- Self-destructs and exits after receiving `410 Gone` repeatedly (the framework no longer recognizes the instance)
- Keeps the log level consistent with the main process via the `LOG_LEVEL` environment variable

## Scaffold tool

Together with the scaffold tool, developers only need to focus on `on_start()` in `ability.py` and `execute()` in `task.py`.

See [Architecture evolution design - Phase 1.5](/en/guide/release-highlights/architecture-evolution#phase-15-ability-project-scaffold-tool).

## Related references

- [Ability instances](/en/guide/release-highlights/instance) — the instance lifecycle state machine
- [Skill document system](/en/guide/release-highlights/skill) — writing AI-Agent instructions for an ability
- [Architecture evolution design](/en/guide/release-highlights/architecture-evolution)
