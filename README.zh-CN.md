# AbilityFramework

[English](README.md) | [简体中文](README.zh-CN.md)

⚙️ 可复用机器人能力的 C++ 宿主，提供包安装、实例生命周期、API 路由、心跳和内嵌管理界面。Semantic Deployment 为每个托管 Robot 运行隔离的宿主。

## 工程结构

- `src/` · `include/`：宿主实现与接口。
- `webui/`：内嵌管理界面源码。
- `test/`：C++ 测试。
- `xmake.lua` · `ci/`：构建配置与打包。

## 🛠 构建

需要 xmake、支持协程的 C++20 编译器、用于构建工具的 Python 3，以及配置中指定的第三方依赖。

当前依赖配方源与 CI 镜像默认配置仍需迁移为公开来源，之后才能认定外部开发者可独立复现构建。具体见 `xmake.lua`；本次文档更新未改变这些构建默认值。

```bash
xmake make-version
xmake f -m release -y
xmake -y
```

可执行产物位于 `build/<platform>/<arch>/release/AbilityFramework`。测试构建使用：

```bash
xmake f -m debug --enable-test=true -y
xmake build test
xmake run test
```

通过 musl 构建环境和静态依赖可生成 Linux 全静态产物；仅设置 `--fwk-static=true` 不会将 glibc 工具链变成可移植的 musl 工具链。准备公开构建环境时，应检查 `ci/build-musl.sh` 与仓库依赖配置。

## 产物使用

进入可执行文件所在目录：

```bash
./AbilityFramework --version
./AbilityFramework -o config.yaml
```

启动前编辑生成配置，选择未占用 HTTP 端口，将 `ABILITY_FRAMEWORK_HOME` 设置为独立实例数据目录，然后执行 `./AbilityFramework -c config.yaml`。管理界面路径为 `/ui`。

默认端口可能与 Semantic Server 的 `8080` 冲突；托管 Robot 部署会独立分配端口与配置。不要让两个宿主共用同一数据目录。

## 常见问题

Ability 代码、Python SDK 依赖与包独立于该可执行文件。宿主静态编译不代表 Python Wheel 或 MuJoCo 依赖也变为静态。能力未就绪时，请检查包 / 运行时版本匹配与宿主日志。

[详细配置与 API 参考](README.reference.md)

[CI 与 Tag 制品发布](docs/ci-release.md)

## 许可证

Copyright 2026 InsightOS。自有代码采用 [Apache-2.0](LICENSE)；第三方组件与资产请查看 [NOTICE](NOTICE) 和[许可范围](LICENSE_SCOPE.md)。

## 三个平台的构建复现

参见 [glibc、musl 与 macOS 构建说明](README.build.md)：包含已锁定的源码版本、实际脚本入口、工具要求、本地与 CI 指令、产物位置和平台验证范围。
