# Zenoh remote execution

AbilityFramework supports launching ability processes remotely through the **package management tool** via the [Zenoh](https://zenoh.io/) protocol.

## How to enable

Set `use_remote_execution: true` in `config.yaml`:

```yaml
use_remote_execution: true   # launch ability processes remotely via zenoh
```

The default is `false` (local execution).

## Differences from local execution

When `use_remote_execution` is `true`, `POST /api/instance` skips the following local steps:

- Manifest lookup (`get_ability_manifest`)
- Ability package download (`task_download_package`)

The ability process is launched on a remote node via the zenoh protocol, and the local framework only sends `lifecycle_request` and maintains the heartbeat state. CR validation also skips the local manifest lookup and package download.

## Applicable scenarios

- The node has no local `packages/` directory; the ability process runs on another node
- Cross-node orchestration: the local framework acts as the control plane, and the remote node as the execution plane

:::warning
Only when `use_remote_execution` is `true` does the ability framework launch ability processes remotely via the `zenoh` protocol through the **package management tool**.
:::

## Related references

- [Ability instances](/en/guide/release-highlights/instance) — the instance creation chain in remote-execution mode
- [Configuration file reference](/en/guide/configuration/config-file) — the `use_remote_execution` field
- [HTTP API reference](/en/api/http-api) — `POST /api/instance`
