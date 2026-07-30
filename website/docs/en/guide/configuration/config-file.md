# Configuration

::: tip
Configuration format: **YAML**

Default filename: `config.yaml`

Generation command: `./AbilityFramework -o config.yaml`
:::

AbilityFramework reads a YAML configuration file on startup that determines the HTTP listen address, the logging backend, the scheduled-task intervals of submodules, network discovery and teaming policies, and runtime parameters such as the WebUI. This document explains the fields one by one.

## Locating the configuration file

The framework determines the home directory (**home**) with the following priority; the configuration file, logs, databases, and ability packages all live under this directory:

1. If the environment variable `ABILITY_FRAMEWORK_HOME` is set, that path is used as home;
2. Otherwise the process's current working directory is used.

The configuration file path is determined by the `-c` argument:

 When `-c` is omitted, `<home>/config.yaml` is read;
- when `-c` is a plain filename (no `/`), it is resolved to `<home>/<filename>`;
- when `-c` is a path (absolute or containing `/`), it is used directly.

## Directories created on startup

On startup with `-c`, the framework automatically creates the following subdirectories under home:

| Directory | Purpose |
|---|---|
| `<home>/log` | Log files |
| `<home>/packages` | Ability package storage |
| `<home>/crs` | Custom resources (CR) |
| `<home>/databases` | SQLite database (`ability_framework.db`) |

If a `<home>/www` directory exists, it is mounted as the WebUI static files (taking priority over the embedded frontend).

## Hot reloading

The framework caches the configuration file based on a content hash: each time the configuration is read, the file hash is compared, and if the content changed it is re-parsed, so new configuration takes effect without a restart.

---

## Full configuration example

Export the full template with `./AbilityFramework -df` (`-d` prints to the terminal + `-f` includes all options):

```yaml
framework_name: robot_node_1   # framework instance name
http_ip: 0.0.0.0              # framework HTTP service IP
http_port: 8080               # framework HTTP service port

use_remote_execution: false    # remote execution (launch ability processes remotely via zenoh); local by default

log:                            # logging configuration
  glog:                         # glog configuration
    color_log: true             # colored logs
    also_log_to_stderr: true    # also write to stderr and log file
    max_log_size: 1024          # maximum log file size, in MB
    stop_logging_if_full_disk: true # stop writing when disk is full
    log_dir: log
  # insightos-log:              # mutually exclusive with glog
  #   level: INFO
  #   service_type: ability
  #   service_name: AbilityFramework
  #   instance_id: framework_1
  #   output: dual              # console / file / dual
  #   log_root: ./logs
  #   buffer_size: 8192
  #   flush_interval: 2
  #   enable_tracing: true
  #   file:
  #     max_size: 10485760
  #     max_files: 10

controller_mgr:               # controller module configuration
  fetch_interval: 10          # scheduled task fetch_and_check_abilities

resource_mgr:
  update_interval: 10         # scheduled task update

lifecycle_mgr:                # lifecycle module configuration
  clear_stale_heartbeats_interval: 10  # scheduled task clear_stale_heartbeats

discovery_mgr:                # discovery module configuration
  methods:                    # network discovery methods
    ipv4: true
    ipv6: false
  expiry: 20                  # discovery timeout, in seconds
  teams:                      # team configuration; when unset, the node does not join a network
    - teamName: team_a        # team name (no spaces)
      master: true            # whether this is the master node
      teamID: 3DF3A930-B100-5F8E-BE60-5884D55E5B70
      secret: a8f5f167f44f4964e6c998dee827110c
  election:                   # election configuration; when unset, election is disabled
    method: bully
    params:
      weight: 20

source_urls: []               # list of ability package download URLs

opentelemetry:                # OpenTelemetry tracing (optional)
  url:                        # remote OTel Collector address

webui:                        # management console configuration
  enabled: true               # whether to enable the WebUI (access /ui)
  custom_path:                # custom frontend static file path (optional); when set, external files replace the embedded WebUI

model_repo:                   # model repository configuration (optional)
  hostname: localhost
  port: 8000
  user:
  password:
```

---

## Field details

### Top-level parameters

| Field | Type | Default | Description |
|---|---|---|---|
| `framework_name` | string | `robot_node_1` | Framework instance name, used to generate the `framework_id` (a name-based UUID). Falls back to a random name `fwk-tmp-<N>` when unset. |
| `http_ip` | string | `0.0.0.0` | HTTP service bind IP. |
| `http_port` | int | `8080` | HTTP service port. Before startup it checks whether the port is in use; if so, it exits directly. |
| `use_remote_execution` | bool | `false` | Whether to enable remote execution. When `true`, ability processes are launched remotely via the zenoh protocol, and CR validation skips the local manifest lookup and package download (for scenarios without a local package). |
| `source_urls` | list | `[]` | List of ability package download URLs. |

:::warning
Only when `use_remote_execution` is `true` does the ability framework launch ability processes remotely via the `zenoh` protocol through the **package management tool**.
:::

### `log` — logging configuration

`log` is a map that **must and can only** contain exactly one of `glog` or `insightos-log`. Having both or neither causes startup failure.

> Even when business logging switches to `insightos-log`, the framework still initializes a minimal glog environment in compatibility mode (writing to `<home>/log/legacy-glog`) to support assertion macros such as `CHECK`.

#### `log.glog`

| Field | Type | Default | Description |
|---|---|---|---|
| `color_log` | bool | `true` | Colored stderr logs. |
| `also_log_to_stderr` | bool | `false` | Also write to stderr and the log file. The `--log-to-stdout` argument forces this on. |
| `max_log_size` | int | `1024` | Maximum size of a single log file, in MB. |
| `stop_logging_if_full_disk` | bool | `true` | Stop writing when the disk is full. |
| `log_dir` | string | `log` | Log directory; relative paths are resolved against home. |

#### `log.insightos-log`

| Field | Type | Default | Description |
|---|---|---|---|
| `level` | string | `INFO` | Log level: `TRACE` / `DEBUG` / `INFO` / `WARN` / `ERROR` / `FATAL` (`WARNING` is equivalent to `WARN`). |
| `service_type` | string | `ability` | Service type identifier. |
| `service_name` | string | *(required)* | Service name, **must not be empty**, otherwise startup fails. |
| `instance_id` | string | value of `framework_name` | Instance ID; falls back to the top-level `framework_name` when unset. |
| `output` | string | `stdout` | Output mode: `console`/`stdout` (console only), `file`/`rotate_file` (file only), `dual` (both). |
| `log_root` | string | *(see below)* | Log root directory; relative paths are resolved against home. Falls back to `$HOME/insightoslog`, or `/tmp/insightoslog` if `$HOME` does not exist. |
| `buffer_size` | int | `4096` | Log buffer size (bytes). |
| `flush_interval` | int | `2` | Flush interval (seconds). |
| `enable_tracing` | bool | `true` | Whether to enable tracing. |
| `stdout_fields` | list | `["event", "caller"]` | Fields written to stdout. |

##### `log.insightos-log.file`

| Field | Type | Default | Description |
|---|---|---|---|
| `max_size` | int | `5242880` (5 MB) | Maximum size of a single log file (bytes). |
| `max_files` | int | `3` | Maximum number of files retained. |
| `archive_compression` | string | `none` | Archive compression method. |

### Submodule scheduled tasks

| Field path | Type (default) | Default | Description |
|---|---|---|---|
| `controller_mgr.fetch_interval` | int (seconds) | `10` | Interval of the ControllerManager `fetch_and_check_abilities` scheduled task. |
| `resource_mgr.update_interval` | int (seconds) | `10` | Interval of the ResourceManager `update` scheduled task. |
| `lifecycle_mgr.clear_stale_heartbeats_interval` | int (seconds) | `10` | Interval of the LifecycleManager stale-heartbeat cleanup scheduled task. |

### `discovery_mgr` — network discovery and teaming

| Field path | Type | Default | Description |
|---|---|---|---|
| `discovery_mgr.methods.ipv4` | bool | `true` | Enable IPv4 discovery. |
| `discovery_mgr.methods.ipv6` | bool | `false` | Enable IPv6 discovery. |
| `discovery_mgr.expiry` | int (seconds) | `30` | Discovery information timeout; cleaned up after it expires. In the export template this value is `20`. |

#### `discovery_mgr.teams` — team configuration (optional)

When `teams` is not configured, the node does not join a network. When configured, the node joins the network according to the team information:

| Field | Type | Description |
|---|---|---|
| `teamName` | string | Team name, no spaces. |
| `master` | bool | Whether this is the master node. |
| `teamID` | string (UUID) | Unique team identifier. |
| `secret` | string | Team secret. |

#### `discovery_mgr.election` — election configuration (optional)

When `election` is not configured, election is disabled:

| Field path | Type | Default | Description |
|---|---|---|---|
| `election.method` | string | `bully` | Election algorithm. |
| `election.params.weight` | int | *(none)* | Node election weight. |

### `opentelemetry` — tracing (optional)

| Field | Type | Description |
|---|---|---|
| `opentelemetry.url` | string | Remote OTel Collector address. When unset, tracing is not enabled. |

### `webui` — management console

| Field | Type | Default | Description |
|---|---|---|---|
| `webui.enabled` | bool | `true` | Whether to enable the WebUI (access `/ui`). When `false`, no frontend is mounted. |
| `webui.custom_path` | string | *(empty)* | Custom frontend static file path. When set (and the path exists), this directory replaces the embedded WebUI. |

> WebUI mount priority: `<home>/www` directory > `webui.custom_path` > embedded frontend.

### `model_repo` — model repository (optional)

Used to integrate an external model repository. When the `model_repo` node is not configured, the framework does not initialize the model repository client.

| Field | Type | Default | Description |
|---|---|---|---|
| `model_repo.hostname` | string | `localhost` | Model repository hostname. |
| `model_repo.port` | int | `8000` | Model repository port. |
| `model_repo.user` | string | *(empty)* | Authentication username. |
| `model_repo.password` | string | *(empty)* | Authentication password. |

---

## Configuration access API

The configuration file can be read at runtime through an HTTP endpoint:

```bash
# Returns the full configuration as JSON (YAML is converted automatically)
curl http://localhost:8080/api/config
```

When the configuration file is missing, this endpoint returns 404.
