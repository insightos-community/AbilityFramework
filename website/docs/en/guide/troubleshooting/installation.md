# Installation problems

## Port in use

Startup fails with exit code `-1` (255); the log reports an HTTP port binding failure.

```bash
# check port usage
lsof -i :8080

# start on a different port
./AbilityFramework -c config.yaml
# change http_port in config.yaml
```

## Working-directory creation failure

The framework auto-creates the `log/`, `packages/`, `crs/`, and `databases/` subdirectories under `ABILITY_FRAMEWORK_HOME`. If that directory is not writable, startup fails.

```bash
# confirm directory permissions
ls -la $ABILITY_FRAMEWORK_HOME
```

## Logging backend configuration conflict

The `log` node must contain exactly one of `glog` or `insightos-log`. Having both or neither causes startup failure.

## Related references

- [CLI command reference](/en/guide/cli) — exit code meanings
- [Configuration file reference](/en/guide/configuration/config-file) — the `log` node
- [Logging system](/en/guide/release-highlights/log) — logging backend selection
