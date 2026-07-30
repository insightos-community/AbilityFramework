# 配置

本文档介绍 AbilityFramework 的命令行选项与配置文件格式。

## 命令行选项

```
AbilityFramework [options]
```

| 选项 | 说明 |
| ------ | ------ |
| `-h, --help` | 显示帮助信息 |
| `-v, --version` | 显示版本信息 |
| `-c, --config <path>` | 指定配置文件路径 |
| `-d, --dump` | 输出基础配置到终端 |
| `-o, --output <file>` | 输出基础配置到文件 |
| `-f, --full` | 与 `-d`/`-o` 配合使用，输出包含所有可选项的完整配置 |
| `-V, --verbose <level>` | 设置详细日志级别（默认 0） |

查看或导出配置模板：

```sh
./AbilityFramework -d              # 查看基础配置
./AbilityFramework -d --full       # 查看完整配置
./AbilityFramework -o config.yaml  # 导出基础配置
./AbilityFramework -o config.yaml --full  # 导出完整配置
```

## 配置文件

### 基础配置

以下字段为框架启动所需：

```yaml
framework_name: robot_node_1         # 框架实例名称
http_ip: 0.0.0.0                     # HTTP 服务监听地址
http_port: 8080                      # HTTP 服务监听端口

log:                                 # 日志配置
  glog:
    color_log: true                  # 彩色日志输出
    also_log_to_stderr: true         # 同时输出到 stderr 和日志文件
    max_log_size: 1024               # 单个日志文件最大大小（MB）
    stop_logging_if_full_disk: true  # 磁盘满时停止写入日志
    log_dir: log                     # 日志输出目录

controller_mgr:                      # 控制器管理模块
  fetch_interval: 10                 # 定时检查能力状态的间隔（秒）

resource_mgr:                        # 资源管理模块
  update_interval: 10                # 定时更新资源的间隔（秒）

lifecycle_mgr:                       # 生命周期管理模块
  clear_stale_heartbeats_interval: 10  # 清理过期心跳的间隔（秒）

discovery_mgr:                       # 组网发现模块
  methods:
    ipv4: true                       # 启用 IPv4 发现
    ipv6: false                      # 启用 IPv6 发现
  expiry: 20                         # 发现超时时间（秒）

webui:                               # 管理控制台
  enabled: true                      # 是否开启 WebUI（访问 /ui）
```

### 可选配置

以下配置项不影响框架启动，可按需添加：

```yaml
source_urls:                         # 能力包下载源地址列表，未配置时无法下载远程能力包
  - ftp://192.168.0.103

discovery_mgr:
  teams:                             # 队伍配置，未配置时不参与组网
    - teamName: team_a               # 队伍名称（不可含空格）
      master: true                   # 是否为主节点
      teamID: <uuid>                 # 队伍唯一标识
      secret: <secret>               # 队伍认证密钥
  election:                          # 选举配置，未配置时不启用选举
    method: bully                    # 选举算法
    params:
      weight: 20                     # 节点权重

opentelemetry:                       # OpenTelemetry 链路追踪（可选）
  url: http://<otel-collector>:4318/v1/traces  # 远端 OTel Collector 地址

webui:                               # 管理控制台（可选）
  custom_path: /path/to/webui        # 自定义前端静态文件路径，配置后使用外部文件替代内嵌 WebUI
```
