# 部署

本章节介绍 AbilityFramework 在生产环境中的部署方式。

## 二进制构建

AbilityFramework 使用 xmake 作为主构建系统：

```bash
xmake config -m release
xmake build
```

构建会自动生成 `version.hpp`（git 版本号）和 `webui_embedded.cpp`（嵌入前端）。

CI 还提供 musl 静态链接构建（`ci/build-musl.sh`），产出完全静态链接的二进制。

## 启动服务

```bash
# 使用默认 config.yaml
./AbilityFramework

# 指定配置文件
./AbilityFramework -c config.yaml

# 设置工作主目录
export ABILITY_FRAMEWORK_HOME=/opt/ability-framework
./AbilityFramework -c config.yaml
```

启动成功后，控制台会打印服务地址。按 `Ctrl+C` 触发优雅关闭。

## 生产配置要点

- 设置 `framework_name` 为有意义的节点标识
- 根据网络拓扑配置 `discovery_mgr.methods`（IPv4/IPv6）
- 如需多机协作，配置 `discovery_mgr.teams` 和 `election`
- 按需配置 `opentelemetry` 链路追踪
- 按需配置 `webui.custom_path` 使用外部前端

## 相关参考

- [CLI 命令参考](/guide/cli) — 启动参数与退出码
- [配置文件参考](/guide/configuration/config-file) — 全部配置字段
- [目录结构](/guide/concepts/architecture) — 构建系统说明
