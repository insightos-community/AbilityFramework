# Zenoh 远程执行

AbilityFramework 支持通过 [Zenoh](https://zenoh.io/) 协议远程通过**包管理工具**启动能力进程。

## 启用方式

在 `config.yaml` 中设置 `use_remote_execution: true`：

```yaml
use_remote_execution: true   # 通过 zenoh 远程启动能力进程
```

默认值为 `false`（本地执行）。

## 与本地执行的区别

当 `use_remote_execution` 为 `true` 时，`POST /api/instance` 会跳过以下本地步骤：

- Manifest 查找（`get_ability_manifest`）
- 能力包下载（`task_download_package`）

能力进程通过 zenoh 协议在远端节点上启动，本地框架只负责发送 `lifecycle_request` 和维护心跳状态。CR 校验也会跳过本地 manifest 查找与包下载。

## 适用场景

- 节点没有本地 `packages/` 目录，能力进程在其他节点上运行
- 跨节点编排：本地框架作为控制面，远端节点作为执行面

:::warning
只有 `use_remote_execution` 为 `true` 时，能力框架才会通过 `zenoh` 协议通过**包管理工具**远程启动能力进程。
:::

## 相关参考

- [能力实例](/guide/release-highlights/instance) — 远程执行模式下的实例创建链路
- [配置文件参考](/guide/configuration/config-file) — `use_remote_execution` 字段说明
- [HTTP API 参考](/api/http-api) — `POST /api/instance`
