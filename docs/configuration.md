# Configuration

This document describes the command-line options and the configuration file format for AbilityFramework.

## Command-Line Options

```
AbilityFramework [options]
```

| Option | Description |
| -------- | ------------- |
| `-h, --help` | Show help |
| `-v, --version` | Show version info |
| `-c, --config <path>` | Path to the config file |
| `-d, --dump` | Print the default config to the terminal |
| `-o, --output <file>` | Write the default config to a file |
| `-f, --full` | With `-d`/`-o`, output the complete config including all optional fields |
| `-V, --verbose <level>` | Set verbose log level (default 0) |

Inspect or export the config template:

```sh
./AbilityFramework -d              # print basic config
./AbilityFramework -d --full       # print full config
./AbilityFramework -o config.yaml  # export basic config
./AbilityFramework -o config.yaml --full  # export full config
```

## Configuration File

### Basic Config

These fields are required for the framework to start:

```yaml
framework_name: robot_node_1         # framework instance name
http_ip: 0.0.0.0                     # HTTP listen address
http_port: 8080                      # HTTP listen port

log:                                 # logging
  glog:
    color_log: true                  # colored log output
    also_log_to_stderr: true         # also output to stderr and the log file
    max_log_size: 1024               # max single log file size (MB)
    stop_logging_if_full_disk: true  # stop writing logs when disk is full
    log_dir: log                     # log output directory

controller_mgr:                      # controller manager
  fetch_interval: 10                 # interval to check ability status (seconds)

resource_mgr:                        # resource manager
  update_interval: 10                # interval to update resources (seconds)

lifecycle_mgr:                       # lifecycle manager
  clear_stale_heartbeats_interval: 10  # interval to clear stale heartbeats (seconds)

discovery_mgr:                       # mesh discovery
  methods:
    ipv4: true                       # enable IPv4 discovery
    ipv6: false                      # enable IPv6 discovery
  expiry: 20                         # discovery timeout (seconds)

webui:                               # management console
  enabled: true                      # enable the WebUI (accessible at /ui)
```

### Optional Config

These fields are optional and do not affect framework startup:

```yaml
source_urls:                         # ability package download sources; remote packages can't be downloaded if unset
  - ftp://192.168.0.103

discovery_mgr:
  teams:                             # team config; the node won't join a mesh if unset
    - teamName: team_a               # team name (no spaces)
      master: true                   # whether this is a master node
      teamID: <uuid>                 # team unique identifier
      secret: <secret>               # team authentication secret
  election:                          # election config; election disabled if unset
    method: bully                    # election algorithm
    params:
      weight: 20                     # node weight

opentelemetry:                       # OpenTelemetry tracing (optional)
  url: http://<otel-collector>:4318/v1/traces  # remote OTel Collector address

webui:                               # management console (optional)
  custom_path: /path/to/webui        # custom frontend static file path; uses external files instead of the embedded WebUI
```
