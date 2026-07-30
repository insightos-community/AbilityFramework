# 故障排查

本章节帮助您诊断和解决 AbilityFramework 运行中遇到的问题。

## 常见问题分类

- [安装问题](/guide/troubleshooting/installation) — 构建、依赖、二进制运行相关
- [网络问题](/guide/troubleshooting/networking) — 组网发现、选举、跨节点通信
- [常见问题](/guide/troubleshooting/faq) — 高频问答

## 快速诊断

### 框架是否正常启动

```bash
curl http://localhost:8080/api/hello
# 预期: "this is ability framework"
```

### 查看运行日志

```bash
curl 'http://localhost:8080/api/log?lines=50&level=WARNING'
```

### 查看配置

```bash
curl http://localhost:8080/api/config | jq
```

## 相关参考

- [HTTP API 参考](/api/http-api) — `/api/hello`、`/api/log`、`/api/config` 等诊断端点
- [CLI 命令参考](/guide/cli) — `--verbose`、`--log-to-stdout` 等调试参数
