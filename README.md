# AbilityFramework

[English](README.md) | [简体中文](README.zh-CN.md)

⚙️ A C++ host for reusable robot abilities: package installation, instance lifecycle, API routing, heartbeats, and an embedded management UI. Semantic Deployment runs an isolated host for each managed Robot.

## Structure

- `src/` · `include/` — host implementation and interfaces.
- `webui/` — embedded management UI sources.
- `test/` — C++ tests.
- `xmake.lua` · `ci/` — build configuration and packaging.

## 🛠 Build

Requires xmake, a C++20 compiler with coroutine support, Python 3 for build tooling, and the configured third-party dependencies.

The current dependency-recipe and CI-image defaults still need migration to public sources before an independent external build can be considered reproducible. See the dependency configuration in `xmake.lua`; this documentation update does not change those defaults.

```bash
xmake make-version
xmake f -m release -y
xmake -y
```

The executable is produced under `build/<platform>/<arch>/release/AbilityFramework`. Test builds use:

```bash
xmake f -m debug --enable-test=true -y
xmake build test
xmake run test
```

A fully static Linux build is supported with the musl build environment and static dependencies; `--fwk-static=true` alone does not turn a glibc toolchain into a portable musl toolchain. Review `ci/build-musl.sh` and repository dependency configuration when preparing a public build environment.

## Use the binary

From the directory containing the built executable:

```bash
./AbilityFramework --version
./AbilityFramework -o config.yaml
```

Before starting, edit the generated configuration, select an unused HTTP port, and set `ABILITY_FRAMEWORK_HOME` to a dedicated instance data directory. Then run `./AbilityFramework -c config.yaml`. The management UI is served at `/ui`.

The default port can conflict with Semantic Server's `8080`; managed Robot deployments allocate their own ports and configuration. Do not run two hosts against the same data directory.

## Troubleshooting

Ability code, Python SDK dependencies, and packages are separate from this executable. A static host does not make Python Wheels or MuJoCo dependencies static. Preserve matching package/runtime versions and inspect host logs when an Ability fails to become healthy.

[Detailed configuration and API reference](README.reference.md)

[CI and Tag releases](docs/ci-release.md)

## License

Copyright 2026 InsightOS. First-party code: [Apache-2.0](LICENSE). See [NOTICE](NOTICE) and [license scope](LICENSE_SCOPE.md) for third-party components and assets.

## Reproducible platform builds

See [glibc, musl and macOS build instructions](README.build.md) for pinned source revisions, exact scripts, tool requirements, local commands, CI reproduction and platform support boundaries.
