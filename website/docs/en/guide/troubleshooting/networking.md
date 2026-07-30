# Networking problems

## Nodes cannot discover each other

### Check the mDNS discovery configuration

```yaml
discovery_mgr:
  methods:
    ipv4: true
    ipv6: false
  expiry: 30
```

Make sure IPv4 discovery is enabled.

### Check the firewall

mDNS uses the multicast address `224.0.0.251:5353` (IPv4); make sure the firewall is not blocking multicast traffic.

### Check the discovery API

```bash
curl http://localhost:8080/api/discovery
curl http://localhost:8080/api/team/peers
```

## Team join failure

```bash
curl http://localhost:8080/api/team
```

When JWT validation fails, POST /api/team/join returns an error. Make sure the teamID matches the team configuration and the jwt token is valid and not expired.

## Election anomalies

```bash
curl http://localhost:8080/api/team/masters
```

The election algorithm uses the Bully algorithm and decides the master based on the weight value. Make sure each node has a different weight value.

## Related references

- [Discovery manager](/en/guide/concepts/architecture)
- [Configuration file reference](/en/guide/configuration/config-file)
- [HTTP API reference](/en/api/http-api)
