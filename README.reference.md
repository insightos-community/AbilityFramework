> Historical technical reference / 历史技术参考。For current build and usage instructions, see [English](README.md) / [中文](README.zh-CN.md). Version-specific examples below are not a current release manifest.

# AbilityFramework

AbilityFramework 是一个基于事件驱动架构的能力管理框架，用于机器人节点上能力包的部署、运行、监控与调度。框架提供 HTTP REST API 和内嵌 WebUI 管理控制台，支持能力资源管理、任务执行引擎、心跳监控、组网发现等功能。

## 功能

- **资源管理** — 管理能力包、能力/设备自定义资源（CR/CRD）、模型信息及存储
- **任务引擎** — 基于工厂模式的异步任务执行与状态跟踪
- **生命周期管理** — 能力实例心跳监控与生命周期状态维护
- **组网发现** — 支持 IPv4/IPv6 多播，基于 JWT 的队伍认证
- **控制器管理** — 监控控制器进程运行状态，定时检查能力可用性
- **子进程管理** — 基于 libuv 事件循环的进程创建与生命周期管理
- **告警管理** — 基于 SQLite 的能力告警记录与查询
- **消息总线** — 模块间消息通信
- **WebUI 管理控制台** — 内嵌于二进制，提供框架调试、能力查询、生命周期操作、业务调用等功能

## 获取

### 从源码编译

#### 依赖

- C++20 编译器（需支持协程）
- [xmake](https://xmake.io/) 构建工具
- Python 3（构建时自动嵌入 WebUI）

#### 编译

```sh
xmake config -m release
xmake build
```

构建过程会自动：
- 从 `include/util/version.hpp.in` 生成 `include/version.hpp`（包含构建日期和 git commit hash）
- 从 `webui/` 目录生成 `src/webui_embedded.cpp`（将前端文件嵌入二进制）

#### 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--fwk-static` | false | 以全静态方式链接所有库，用于交叉编译 |
| `--use-cpptrace` | false | 启用 cpptrace，用于框架 debug |
| `--enable-test` | false | 启用单元测试（依赖 doctest） |

示例：

```sh
xmake config -m debug --enable-test
xmake build test
xmake run test
```

## 使用

### 命令行选项

```
AbilityFramework [options]
```

| 选项 | 说明 |
|------|------|
| `-h, --help` | 显示帮助信息 |
| `-v, --version` | 显示版本信息 |
| `-c, --config <path>` | 指定配置文件路径 |
| `-d, --dump` | 输出基础配置到终端 |
| `-o, --output <file>` | 输出基础配置到文件 |
| `-f, --full` | 与 `-d`/`-o` 配合使用，输出包含所有可选项的完整配置 |
| `-V, --verbose <level>` | 设置详细日志级别（默认 0） |

### 快速启动

```sh
# 导出默认配置
./AbilityFramework -o config.yaml

# 启动框架
export ABILITY_FRAMEWORK_HOME=$(pwd)
./AbilityFramework -c config.yaml

# 打开管理控制台
# 浏览器访问 http://localhost:8080/ui
```

### 配置文件

运行前需准备 `config.yaml` 配置文件。可通过 `-d` 参数查看配置模板，或通过 `-o` 参数导出：

```sh
# 查看基础配置
./AbilityFramework -d

# 查看包含所有可选项的完整配置
./AbilityFramework -d --full

# 导出基础配置到文件
./AbilityFramework -o config.yaml

# 导出完整配置到文件
./AbilityFramework -o config.yaml --full
```

#### 基础配置

```yaml
framework_name: robot_node_1       # 框架实例名称
http_ip: 0.0.0.0                   # HTTP 服务监听地址
http_port: 8080                    # HTTP 服务监听端口

log:                               # 日志配置
  glog:
    color_log: true                # 彩色日志输出
    also_log_to_stderr: true       # 同时输出到 stderr 和日志文件
    max_log_size: 1024             # 单个日志文件最大大小（MB）
    stop_logging_if_full_disk: true  # 磁盘满时停止写入日志
    log_dir: log                   # 日志输出目录

controller_mgr:                    # 控制器管理模块
  fetch_interval: 10               # 定时检查能力状态的间隔（秒）

resource_mgr:                      # 资源管理模块
  update_interval: 10              # 定时更新资源的间隔（秒）

lifecycle_mgr:                     # 生命周期管理模块
  clear_stale_heartbeats_interval: 10  # 清理过期心跳的间隔（秒）

discovery_mgr:                     # 组网发现模块
  methods:
    ipv4: true                     # 启用 IPv4 发现
    ipv6: false                    # 启用 IPv6 发现
  expiry: 20                       # 发现超时时间（秒）

webui:                             # 管理控制台
  enabled: true                    # 是否开启 WebUI（访问 /ui）
```

#### 可选配置

以下配置项不影响框架启动，可按需添加：

```yaml
source_urls:                       # 能力包下载源地址列表，未配置时无法下载远程能力包
  - https://packages.example.com

discovery_mgr:
  teams:                           # 队伍配置，未配置时不参与组网
    - teamName: team_a             # 队伍名称（不可含空格）
      master: true                 # 是否为主节点
      teamID: <uuid>               # 队伍唯一标识
      secret: <secret>             # 队伍认证密钥
  election:                        # 选举配置，未配置时不启用选举
    method: bully                  # 选举算法
    params:
      weight: 20                   # 节点权重

opentelemetry:                     # OpenTelemetry 链路追踪（可选）
  url: http://<otel-collector>:4318/v1/traces  # 远端 OTel Collector 地址

webui:                             # 管理控制台（可选）
  custom_path: /path/to/webui      # 自定义前端静态文件路径，配置后使用外部文件替代内嵌 WebUI
```

### WebUI 管理控制台

框架内嵌 WebUI，启动后访问 `http://<host>:<port>/ui` 即可使用，无需额外部署前端文件。

功能区块：

| 区块 | 功能 |
|------|------|
| **框架调试** | 状态检查、配置查看、CRD/包列表、CR 文件、告警记录、组网信息、能力包上传 |
| **能力查询** | 能力/设备/服务实例列表、实时状态、实例详情、删除操作 |
| **生命周期** | 心跳实时监控（自动刷新）、生命周期命令发送、创建 CR |
| **业务调用** | 选择运行中的能力、查看已注册 task、执行 task、查询结果 |

WebUI 行为由 `webui` 配置控制：

| 配置 | 效果 |
|------|------|
| `webui.enabled: true`（默认） | 开启内嵌 WebUI |
| `webui.enabled: false` | 关闭 WebUI |
| `webui.custom_path: /path` | 使用外部前端文件替代内嵌版本（用于开发调试） |

### 工作目录

框架通过环境变量 `ABILITY_FRAMEWORK_HOME` 确定工作目录，未设置时默认为当前工作目录。启动时会自动创建以下目录结构：

```
ABILITY_FRAMEWORK_HOME/
├── config.yaml                          # 配置文件
├── packages/                            # 能力包存放目录
│   └── <package_name>/
│       └── <version>/                   # 版本号须符合 semver 规范
│           ├── package.yaml             # 包清单（必需）
│           ├── ability.manifest.yaml    # 能力清单（必需）
│           └── bin/
│               └── ability              # 可执行文件
├── crs/                                 # CR 实例定义文件（YAML），框架定时扫描加载
├── databases/                           # SQLite 数据库（ability_framework.db）
└── log/                                 # 日志文件
```

`package.yaml` 清单格式：

```yaml
name: my.ability.org       # 包名
version: 1.0.0             # 语义化版本
arch: x86_64               # 目标架构（须与运行主机匹配）
```

`ability.manifest.yaml` 为能力清单，描述能力的接口、schema、依赖等信息。CRD 由框架内置，能力包只需携带 manifest。

**CR 自动加载：** 框架每隔 `resource_mgr.update_interval` 秒扫描 `crs/` 目录，自动加载其中的 YAML 文件。CR 中设置 `spec.autoStart: true` 的实例会被自动启动。

### 添加能力实例

框架通过 HTTP REST API 管理能力实例，典型流程如下：

#### 1. 上传能力包

```sh
curl -X POST http://localhost:8080/api/package \
  -F "file=@my_ability.zip;type=application/zip"
```

上传后框架会自动解压到 `packages/<package_name>/<version>/` 目录。也可通过 WebUI 的框架调试页面上传。

#### 2. 创建并启动能力实例

```sh
curl -X POST "http://localhost:8080/api/cr?start=true&connect=true" \
  -H "Content-Type: application/json" \
  -d '{
    "kind": "AtomAbility",
    "metadata": { "name": "my-instance" },
    "spec": {
      "package": "my.ability.org",
      "version": "1.0.0",
      "abilityName": "MyAbility.org",
      "position": "localhost"
    }
  }'
```

- `start=true` — 创建后自动启动
- `connect=true` — 启动后自动连接

返回值包含 `taskId`，可用于查询异步任务进度。

如果本地不存在该能力包，框架会自动从 `source_urls` 配置的地址下载。

#### 3. 查看运行状态

```sh
# 查看所有能力实例的心跳
curl http://localhost:8080/api/ability-heartbeat

# 查看指定实例
curl http://localhost:8080/api/ability-heartbeat/<instance-id>
```

#### 4. 停止能力实例

```sh
curl -X POST http://localhost:8080/api/lifecycle-request \
  -H "Content-Type: application/json" \
  -d '{
    "abilityInstanceId": "<instance-id>",
    "command": "terminate"
  }'
```

#### 5. 删除能力实例

```sh
curl -X DELETE "http://localhost:8080/api/cr/<instance-id>?force=true"
```

## 许可证

见 [LICENSE](LICENSE)、[NOTICE](NOTICE) 与 [许可范围](LICENSE_SCOPE.md)。
