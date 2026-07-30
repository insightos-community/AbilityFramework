<div align="center">

# AbilityFramework

**基于事件驱动架构的能力管理框架，用于机器人节点上能力包的部署、运行、监控与调度。**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![xmake](https://img.shields.io/badge/build-xmake-green.svg)](https://xmake.io/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](#许可证)

[English](README.md) | **中文**

</div>

AbilityFramework 提供 HTTP REST API 和内嵌 WebUI 管理控制台，支持能力资源管理、任务执行引擎、心跳监控、组网发现等功能，全部集成于单一二进制文件中。

---

## 它如何工作

### 架构图

<div  align="center">
<img src="./docs/images/abilityframework_arch.png" width = "85%" align="center">
</div>

### 工作流程

能力包的完整生命周期分为五个阶段——Schema 定义、工程生成、实现与打包、部署、MCP 调用。流程图与分步说明见[第一个项目](website/docs/zh/getting-started/first-project.md)。

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

### 从 Release 下载

前往项目的 [Releases](/insight-os/AbilityFramework/-/releases) 页面，下载对应平台的预编译二进制文件。

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
| ------ | -------- | ------ |
| `--fwk-static` | false | 以全静态方式链接所有库，用于交叉编译 |
| `--use-cpptrace` | false | 启用 cpptrace，用于框架调试 |
| `--enable-test` | false | 启用单元测试（依赖 doctest） |

示例 — 编译并运行测试套件：

```sh
xmake config -m debug --enable-test
xmake build test
xmake run test
```

## 快速启动

```sh
# 导出默认配置
./AbilityFramework -o config.yaml

# 启动框架
export ABILITY_FRAMEWORK_HOME=$(pwd)
./AbilityFramework -c config.yaml

# 打开管理控制台
# 浏览器访问 http://localhost:8080/ui
```

完整的命令行选项与配置字段（基础配置 + 可选配置）请参考 [配置说明](docs/configuration_zh.md)。

## WebUI 管理控制台

框架内嵌 WebUI，启动后访问 `http://<host>:<port>/ui` 即可使用，无需额外部署前端文件。

| 区块 | 功能 |
| ------ | ------ |
| **框架调试** | 状态检查、配置查看、CRD/包列表、CR 文件、告警记录、组网信息、能力包上传 |
| **能力查询** | 能力/设备/服务实例列表、实时状态、实例详情、删除操作 |
| **生命周期** | 心跳实时监控（自动刷新）、生命周期命令发送、创建 CR |
| **业务调用** | 选择运行中的能力、查看已注册 task、执行 task、查询结果 |

WebUI 行为由 `webui` 配置控制：

| 配置 | 效果 |
| ------ | ------ |
| `webui.enabled: true`（默认） | 开启内嵌 WebUI |
| `webui.enabled: false` | 关闭 WebUI |
| `webui.custom_path: /path` | 使用外部前端文件替代内嵌版本（用于开发调试） |

## 工作目录

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

## 文档

详细的参考文档位于 `docs/` 目录下：

- [配置说明](docs/configuration_zh.md) — 命令行选项与完整的配置文件参考
- [REST API](docs/rest-api_zh.md) — 通过 HTTP API 管理能力实例生命周期（上传、创建、启动、停止、删除）
- [路线图](docs/roadmap_zh.md) — 季度演进甘特图与分季度特性明细

## 路线图

以下方向正在规划与开发中，具体细节可能随设计演进而调整。
完整的季度演进路线图（含甘特图与分季度特性明细）见 [路线图](docs/roadmap_zh.md)。

## 许可证

本项目基于 [Apache License 2.0](LICENSE) 开源。
