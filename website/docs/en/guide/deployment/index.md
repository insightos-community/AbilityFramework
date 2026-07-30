# Deployment

This section covers how to deploy AbilityFramework in production.

## Building the binary

AbilityFramework uses xmake as its primary build system:

```bash
xmake config -m release
xmake build
```

The build automatically generates `version.hpp` (the git version) and `webui_embedded.cpp` (the embedded frontend).

CI also provides a musl static-linking build (`ci/build-musl.sh`) that produces a fully statically linked binary.

## Starting the service

```bash
# Use the default config.yaml
./AbilityFramework

# Specify a configuration file
./AbilityFramework -c config.yaml

# Set the working directory
export ABILITY_FRAMEWORK_HOME=/opt/ability-framework
./AbilityFramework -c config.yaml
```

After a successful startup, the console prints the service address. Press `Ctrl+C` to trigger a graceful shutdown.

## Production configuration checklist

- Set `framework_name` to a meaningful node identifier
- Configure `discovery_mgr.methods` (IPv4/IPv6) according to your network topology
- Configure `discovery_mgr.teams` and `election` if you need multi-node cooperation
- Configure `opentelemetry` tracing as needed
- Configure `webui.custom_path` to use an external frontend as needed

## Related references

- [CLI command reference](/en/guide/cli) — startup arguments and exit codes
- [Configuration file reference](/en/guide/configuration/config-file) — all configuration fields
- [Directory structure](/en/guide/concepts/architecture) — build system details
