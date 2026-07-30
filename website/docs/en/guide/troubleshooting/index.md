# Troubleshooting

This section helps you diagnose and resolve problems you encounter while running AbilityFramework.

## Common problem categories

- [Installation problems](/en/guide/troubleshooting/installation) — building, dependencies, running the binary
- [Networking problems](/en/guide/troubleshooting/networking) — network discovery, election, cross-node communication
- [FAQ](/en/guide/troubleshooting/faq) — frequently asked questions

## Quick diagnostics

### Is the framework running normally

```bash
curl http://localhost:8080/api/hello
# expected: "this is ability framework"
```

### View runtime logs

```bash
curl 'http://localhost:8080/api/log?lines=50&level=WARNING'
```

### View the configuration

```bash
curl http://localhost:8080/api/config | jq
```

## Related references

- [HTTP API reference](/en/api/http-api) — diagnostic endpoints such as `/api/hello`, `/api/log`, `/api/config`
- [CLI command reference](/en/guide/cli) — debugging arguments such as `--verbose`, `--log-to-stdout`
