# 常见问题

## 能力实例启动后立即消失

检查实例是否因孤儿心跳被清理。框架重启后会把所有非终止状态的实例标记为 Terminated。如果能力进程仍在发心跳但框架已不认识它（410 Gone），SDK 会自毁退出。

```bash
# 查看当前实例列表
curl http://localhost:8080/api/instance

# 查看最近日志
curl 'http://localhost:8080/api/log?lines=100&level=WARNING'
```

## singleton 约束导致启动失败

当 CR 的 `spec.singleton` 为 `true`（缺省值）时，同一能力名全局只允许一个活跃实例。如果已有运行实例，`POST /api/instance` 返回 `409 Conflict`。

```bash
# 先停止现有实例
curl -X DELETE http://localhost:8080/api/instance/<instance_id>
```

## 配置热加载不生效

框架基于内容哈希缓存配置文件。确认文件确实发生了内容变更（而非仅修改时间变化）。

## 能力包上传超过限制

能力包体积上限 100MB，超出返回 413。如需上传更大的包，考虑分拆或压缩。

## autoStart 循环重启

框架通过内存中的 `start_records` 限制同一会话内某个能力的自动启动重试次数不超过 3 次。如果能力持续崩溃，检查能力进程自身的日志。

## 相关参考

- [能力实例](/guide/release-highlights/instance) — 实例生命周期与异常处理
- [配置文件参考](/guide/configuration/config-file) — `singleton`、`autoStart` 配置
- [HTTP API 参考](/api/http-api) — 实例管理 API
