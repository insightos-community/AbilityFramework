# 安装问题

## 端口被占用

启动失败，退出码 `-1`（255），日志提示 HTTP 端口绑定失败。

```bash
# 检查端口占用
lsof -i :8080

# 换端口启动
./AbilityFramework -c config.yaml
# 在 config.yaml 中修改 http_port
```

## 工作目录创建失败

框架会在 `ABILITY_FRAMEWORK_HOME` 下自动创建 `log/`、`packages/`、`crs/`、`databases/` 子目录。如果该目录不可写，启动会失败。

```bash
# 确认目录权限
ls -la $ABILITY_FRAMEWORK_HOME
```

## 日志后端配置冲突

`log` 节点必须且只能包含 `glog` 或 `insightos-log` 其中的一个。两者同时存在或同时缺失都会导致启动失败。

## 相关参考

- [CLI 命令参考](/guide/cli) — 退出码含义
- [配置文件参考](/guide/configuration/config-file) — `log` 节点配置
- [日志系统](/guide/release-highlights/log) — 日志后端选择
