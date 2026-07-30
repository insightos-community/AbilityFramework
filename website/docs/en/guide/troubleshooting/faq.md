# FAQ

## An ability instance disappears immediately after starting

Check whether the instance was cleaned up due to an orphan heartbeat. After a framework restart, all non-terminated instances are marked Terminated. If an ability process is still sending heartbeats but the framework no longer recognizes it (410 Gone), the SDK self-destructs and exits.

```bash
# view the current instance list
curl http://localhost:8080/api/instance

# view the most recent logs
curl 'http://localhost:8080/api/log?lines=100&level=WARNING'
```

## Startup fails due to the singleton constraint

When a CR's `spec.singleton` is `true` (the default), only one active instance of the same ability name is allowed globally. If a running instance already exists, `POST /api/instance` returns `409 Conflict`.

```bash
# stop the existing instance first
curl -X DELETE http://localhost:8080/api/instance/<instance_id>
```

## Configuration hot reload does not take effect

The framework caches the configuration file based on a content hash. Confirm that the file actually changed in content (not just its modification time).

## Ability package upload exceeds the limit

The ability package size limit is 100MB; exceeding it returns 413. To upload a larger package, consider splitting or compressing it.

## autoStart restart loop

The framework uses the in-memory `start_records` to limit the auto-start retry count for an ability within a single session to no more than 3 times. If the ability keeps crashing, check the ability process's own logs.

## Related references

- [Ability instances](/en/guide/release-highlights/instance) — instance lifecycle and exception handling
- [Configuration file reference](/en/guide/configuration/config-file) — `singleton`, `autoStart` configuration
- [HTTP API reference](/en/api/http-api) — instance management API
