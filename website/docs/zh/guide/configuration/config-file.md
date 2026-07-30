# 配置

::: tip
配置格式: **YAML**

默认文件名: `config.yaml`

生成命令: `./AbilityFramework -o config.yaml`
:::

AbilityFramework 启动时读取一个 YAML 配置文件，决定 HTTP 监听地址、日志后端、各子模块的定时任务间隔、网络发现与组网策略，以及 WebUI 等运行参数。本文按字段逐一说明。

## 配置文件定位

框架按以下优先级确定工作主目录（**home**），配置文件、日志、数据库、能力包都放在该目录下：

1. 若设置了环境变量 `ABILITY_FRAMEWORK_HOME`，则使用该路径作为 home；
2. 否则使用进程的当前工作目录。

配置文件路径由 `-c` 参数决定：

- 未传 `-c` 时，读取 `<home>/config.yaml`；
- `-c` 传入纯文件名（不含 `/`）时，解析为 `<home>/<文件名>`；
- `-c` 传入路径（绝对路径或含 `/`）时，直接使用该路径。

## 启动时自动创建的目录

`-c` 启动时，框架会在 home 下自动创建以下子目录：

| 目录 | 用途 |
|---|---|
| `<home>/log` | 日志文件 |
| `<home>/packages` | 能力包存储 |
| `<home>/crs` | 自定义资源（CR） |
| `<home>/databases` | SQLite 数据库（`ability_framework.db`） |

如果 `<home>/www` 目录存在，会作为 WebUI 静态文件挂载点（优先级高于内嵌前端）。

## 配置热加载

框架内部对配置文件做了基于内容哈希的缓存：每次读取配置时会比对文件哈希，若文件内容发生变化则重新解析，无需重启即可使新配置生效。

---

## 完整配置示例

通过 `./AbilityFramework -df`（`-d` 输出到终端 + `-f` 包含全部可选项）可导出完整模板：

```yaml
framework_name: robot_node_1   # 框架实例名称
http_ip: 0.0.0.0              # 框架HTTP服务IP
http_port: 8080               # 框架HTTP服务端口

use_remote_execution: false    # 远程执行（通过 zenoh 远程启动能力进程），默认本地执行

log:                            # 日志配置
  glog:                         # glog配置
    color_log: true             # 彩色日志
    also_log_to_stderr: true    # 同时输出到stderr和日志文件
    max_log_size: 1024          # 日志文件最大大小，单位 MB
    stop_logging_if_full_disk: true # 磁盘满时停止写入
    log_dir: log
  # insightos-log:              # 与 glog 二选一
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

controller_mgr:               # controller模块配置
  fetch_interval: 10          # 定时任务 fetch_and_check_abilities

resource_mgr:
  update_interval: 10         # 定时任务 update

lifecycle_mgr:                # lifecycle模块配置
  clear_stale_heartbeats_interval: 10  # 定时任务 clear_stale_heartbeats

discovery_mgr:                # discovery模块配置
  methods:                    # 网络发现方式
    ipv4: true
    ipv6: false
  expiry: 20                  # 发现超时时间，单位秒
  teams:                      # 队伍配置，未配置时不参与组网
    - teamName: team_a        # 队伍名称（不可含空格）
      master: true            # 是否为主节点
      teamID: 3DF3A930-B100-5F8E-BE60-5884D55E5B70
      secret: a8f5f167f44f4964e6c998dee827110c
  election:                   # 选举配置，未配置时不启用选举
    method: bully
    params:
      weight: 20

source_urls: []               # 能力包下载地址列表

opentelemetry:                # OpenTelemetry 链路追踪（可选）
  url:                        # 远端 OTel Collector 地址

webui:                        # 管理控制台配置
  enabled: true               # 是否开启 WebUI（访问 /ui）
  custom_path:                # 自定义前端静态文件路径（可选），配置后使用外部文件替代内嵌 WebUI

model_repo:                   # 模型仓库配置（可选）
  hostname: localhost
  port: 8000
  user:
  password:
```

---

## 字段详解

### 顶层参数

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `framework_name` | string | `robot_node_1` | 框架实例名称，用于生成 `framework_id`（基于名称的 UUID）。未配置时退化为随机名 `fwk-tmp-<N>`。 |
| `http_ip` | string | `0.0.0.0` | HTTP 服务绑定 IP。 |
| `http_port` | int | `8080` | HTTP 服务端口。启动前会检测该端口是否被占用，若被占用则直接退出。 |
| `use_remote_execution` | bool | `false` | 是否启用远程执行。设为 `true` 时，能力进程通过 zenoh 协议远程启动，CR 校验跳过本地 manifest 查找与包下载（适用于无本地 package 的场景）。 |
| `source_urls` | list | `[]` | 能力包下载地址列表。 |

:::warning
只有 `use_remote_execution` 为 `true` 时，能力框架才会通过 `zenoh` 协议通过**包管理工具**远程启动能力进程。
:::

### `log` — 日志配置

`log` 是一个 map，**必须且只能**包含 `glog` 或 `insightos-log` 其中的一个。两者同时存在或同时缺失都会导致启动失败。

> 即使业务日志切到 `insightos-log`，框架仍会以兼容模式初始化一个最小 glog 环境（日志写入 `<home>/log/legacy-glog`），用于支撑 `CHECK` 等断言宏。

#### `log.glog`

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `color_log` | bool | `true` | stderr 彩色日志。 |
| `also_log_to_stderr` | bool | `false` | 同时输出到 stderr 和日志文件。`--log-to-stdout` 参数会强制开启。 |
| `max_log_size` | int | `1024` | 单个日志文件最大大小，单位 MB。 |
| `stop_logging_if_full_disk` | bool | `true` | 磁盘满时停止写入。 |
| `log_dir` | string | `log` | 日志目录，相对路径基于 home 解析。 |

#### `log.insightos-log`

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `level` | string | `INFO` | 日志级别：`TRACE` / `DEBUG` / `INFO` / `WARN` / `ERROR` / `FATAL`（`WARNING` 等价于 `WARN`）。 |
| `service_type` | string | `ability` | 服务类型标识。 |
| `service_name` | string | *(必填)* | 服务名称，**不可为空**，否则启动失败。 |
| `instance_id` | string | `framework_name` 的值 | 实例 ID，未配置时回退到顶层 `framework_name`。 |
| `output` | string | `stdout` | 输出方式：`console`/`stdout`（仅控制台）、`file`/`rotate_file`（仅文件）、`dual`（两者）。 |
| `log_root` | string | *(见下)* | 日志根目录，相对路径基于 home 解析。未配置时退化为 `$HOME/insightoslog`，`$HOME` 不存在则为 `/tmp/insightoslog`。 |
| `buffer_size` | int | `4096` | 日志缓冲区大小（字节）。 |
| `flush_interval` | int | `2` | 刷盘间隔（秒）。 |
| `enable_tracing` | bool | `true` | 是否启用链路追踪。 |
| `stdout_fields` | list | `["event", "caller"]` | stdout 输出的字段。 |

##### `log.insightos-log.file`

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `max_size` | int | `5242880`（5 MB） | 单个日志文件最大大小（字节）。 |
| `max_files` | int | `3` | 保留的最大文件数。 |
| `archive_compression` | string | `none` | 归档压缩方式。 |

### 子模块定时任务

| 字段路径 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `controller_mgr.fetch_interval` | int（秒） | `10` | ControllerManager 的 `fetch_and_check_abilities` 定时任务间隔。 |
| `resource_mgr.update_interval` | int（秒） | `10` | ResourceManager 的 `update` 定时任务间隔。 |
| `lifecycle_mgr.clear_stale_heartbeats_interval` | int（秒） | `10` | LifecycleManager 清理过期心跳的定时任务间隔。 |

### `discovery_mgr` — 网络发现与组网

| 字段路径 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `discovery_mgr.methods.ipv4` | bool | `true` | 启用 IPv4 发现。 |
| `discovery_mgr.methods.ipv6` | bool | `false` | 启用 IPv6 发现。 |
| `discovery_mgr.expiry` | int（秒） | `30` | 发现信息的超时时间，超时后清理。导出模板中该值为 `20`。 |

#### `discovery_mgr.teams` — 队伍配置（可选）

未配置 `teams` 时，节点不参与组网。配置后节点按队伍信息加入网络：

| 字段 | 类型 | 说明 |
|---|---|---|
| `teamName` | string | 队伍名称，不可含空格。 |
| `master` | bool | 是否为主节点。 |
| `teamID` | string（UUID） | 队伍唯一标识。 |
| `secret` | string | 队伍密钥。 |

#### `discovery_mgr.election` — 选举配置（可选）

未配置 `election` 时不启用选举：

| 字段路径 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `election.method` | string | `bully` | 选举算法。 |
| `election.params.weight` | int | *(无)* | 节点选举权重。 |

### `opentelemetry` — 链路追踪（可选）

| 字段 | 类型 | 说明 |
|---|---|---|
| `opentelemetry.url` | string | 远端 OTel Collector 地址。未配置则不启用。 |

### `webui` — 管理控制台

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `webui.enabled` | bool | `true` | 是否启用 WebUI（访问 `/ui`）。设为 `false` 时不挂载任何前端。 |
| `webui.custom_path` | string | *(空)* | 自定义前端静态文件路径。配置后（且路径存在）使用该目录替代内嵌 WebUI。 |

> WebUI 挂载优先级：`<home>/www` 目录 > `webui.custom_path` > 内嵌前端。

### `model_repo` — 模型仓库（可选）

用于对接外部模型仓库。未配置 `model_repo` 节点时，框架不会初始化模型仓库客户端。

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `model_repo.hostname` | string | `localhost` | 模型仓库主机名。 |
| `model_repo.port` | int | `8000` | 模型仓库端口。 |
| `model_repo.user` | string | *(空)* | 认证用户名。 |
| `model_repo.password` | string | *(空)* | 认证密码。 |

---

---

## 配置访问 API

配置文件在运行时可通过 HTTP 接口读取：

```bash
# 以 JSON 形式返回完整配置（YAML 自动转换）
curl http://localhost:8080/api/config
```

配置文件缺失时该接口返回 404。
