# 网络问题

## 节点无法互相发现

### 检查 mDNS 发现配置

```yaml
discovery_mgr:
  methods:
    ipv4: true
    ipv6: false
  expiry: 30
```

确保启用了 IPv4 发现。

### 检查防火墙

mDNS 使用多播地址 `224.0.0.251:5353`（IPv4），确保防火墙未拦截多播流量。

### 检查 discovery API

```bash
curl http://localhost:8080/api/discovery
curl http://localhost:8080/api/team/peers
```

## 队伍加入失败

```bash
curl http://localhost:8080/api/team
```

JWT 校验失败时 POST /api/team/join 返回错误。确保 teamID 与队伍配置一致，jwt token 有效且未过期。

## 选举异常

```bash
curl http://localhost:8080/api/team/masters
```

选举算法使用 Bully 算法，基于 weight 值决定 master。确保各节点配置了不同的 weight 值。

## 相关参考

- [发现管理器](/guide/concepts/architecture)
- [配置文件参考](/guide/configuration/config-file)
- [HTTP API 参考](/api/http-api)
