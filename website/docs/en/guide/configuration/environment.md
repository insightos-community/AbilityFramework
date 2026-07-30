# Environment variables

AbilityFramework uses environment variables to control the framework working directory and subprocess behavior.

| Variable | Description |
|---|---|
| `ABILITY_FRAMEWORK_HOME` | Framework working directory. When unset, the process's current working directory is used. Logs, ability packages, databases, and other runtime files are stored under this directory. |
| `LOG_LEVEL` | When the framework uses the `insightos-log` backend, it automatically sets this variable and injects it into subprocesses so that their log level matches the main process. |

## Interaction with CLI arguments

The `--log-to-stdout` CLI argument forces `force_console_output = true`: the glog backend enables `also_log_to_stderr`, and the insightos-log backend switches from pure-file output to `dual` (console + file).

## Related references

- [Configuration file reference](/en/guide/configuration/config-file)
- [CLI command reference](/en/guide/cli)
- [Logging system](/en/guide/release-highlights/log)
