# Configuration reference

AbilityFramework reads `config.yaml` from its working directory on startup. Below is the minimal configuration used in this tutorial:

```yaml
framework_name: playground
http_ip: 0.0.0.0
http_port: 8080

log:
  glog:
    color_log: true
    also_log_to_stderr: true
    max_log_size: 1024
    stop_logging_if_full_disk: true
    log_dir: log

resource_mgr:
  update_interval: 5

lifecycle_mgr:
  clear_stale_heartbeats_interval: 10
```

## Field reference

| Field | Description |
|------|------|
| `framework_name` | Framework instance name, used for logging and resource identification |
| `http_ip` | HTTP service listen address |
| `http_port` | HTTP service listen port |
| `log.glog` | glog logging configuration; see [Logging configuration](/en/guide/release-highlights/log) |
| `log.glog.color_log` | Colored log output in the terminal |
| `log.glog.also_log_to_stderr` | Also write logs to stderr |
| `log.glog.max_log_size` | Maximum size of a single log file (KB) |
| `log.glog.stop_logging_if_full_disk` | Stop writing logs when the disk is full |
| `log.glog.log_dir` | Log directory (relative to the working directory) |
| `resource_mgr.update_interval` | Resource manager scan interval (seconds) |
| `lifecycle_mgr.clear_stale_heartbeats_interval` | Interval for clearing stale heartbeats (seconds) |

> For the full set of options (including `insightos-log`, `discovery_mgr`, `source_urls`, etc.), see the [Configuration file reference](/en/guide/configuration/config-file).
