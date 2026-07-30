# Repository Guidelines

This guide helps contributors get up to speed on AbilityFramework, an event-driven ability-management framework for deploying, running, and monitoring ability packages on robot nodes. It exposes an HTTP REST API and an embedded WebUI console.

## Project Structure & Module Organization

The codebase mirrors module directories across `include/` (public headers) and `src/` (implementations):

```
include/<module>/*.hpp   src/<module>/*.cpp
```

Core modules: `resourcemgr` (packages/CRs/CRDs), `taskmgr` (async task engine), `lifecyclemgr` (heartbeat & lifecycle), `controllermgr`, `discoverymgr` (IPv4/IPv6 multicast, JWT), `subprocessmgr` (libuv processes), `abilityalertmgr` (SQLite alerts), `databasemgr`, `messagebus`, `param`, `util`.

- `webui/` — embedded management console (`app.js`, `index.html`, `style.css`, `embed.py`), compiled into the binary at build time.
- `test/` — `cases/*.cpp` doctest unit tests plus standalone `.cpp` and `.py` (HTTP API) tests.
- `docs/`, `ci/` — architecture notes and the `build-musl.sh` cross-build helper.
- Generated at build time (gitignored, do not edit): `include/version.hpp` and `src/webui_embedded.cpp`.

## Build, Test, and Development Commands

The primary build tool is **xmake** (C++20 required); `CMakeLists.txt` exists as a secondary path.

```sh
xmake config -m release && xmake build              # release build
xmake config -m debug --enable-test                  # enable unit tests
xmake build test && xmake run test                   # build & run doctest suite
```

Notable config options: `--fwk-static` (fully static link, for cross-compilation), `--use-cpptrace` (stack traces for debugging), `--enable-test`.

Runtime uses `ABILITY_FRAMEWORK_HOME` for its working directory (defaults to cwd). Export the default config and launch:

```sh
xmake run AbilityFramework -o config.yaml            # export config template
export ABILITY_FRAMEWORK_HOME=$(pwd)
xmake run AbilityFramework -c config.yaml            # console at http://localhost:8080/ui
```

## Coding Style & Naming Conventions

Format C++ with the repo's `.clang-format` (Microsoft base, 4-space indent, 100-column limit, pointers left-aligned). Prefer `snake_case` files and `PascalCase` types, matching the existing headers. Run formatting before committing.

## Testing Guidelines

Unit tests use **doctest** (vendored `test/doctest.h`, no external dependency). Add new tests under `test/cases/` with names like `test_<subject>.cpp` using `TEST_CASE("...")`. Python tests under `test/` are integration checks that exercise the live HTTP API via `requests`; they require a running instance on `localhost:8080`.

## Commit & Pull Request Guidelines

Follow **Conventional Commits** as seen in history: `feat:`, `fix:`, `ci:` prefixes with a concise lowercase summary (e.g., `fix: occupation handler crash on instance_id`). Keep commits focused and one logical change per PR. CI (GitHub Actions) runs the build workflow on push/tag, so ensure `xmake build` and `xmake run test` pass locally before pushing.

## Static musl Builds (GitHub Actions)

`.github/workflows/build-musl.yml` produces fully static musl binaries for `x86_64` and `arm64`. Each architecture builds natively inside an Alpine Linux container on its matching GitHub runner — no cross-compiler or QEMU needed. The build logic lives in [ci/build-musl.sh](ci/build-musl.sh). To reproduce a release build locally (requires Docker):

```sh
docker run --rm --platform linux/arm64 \
    -v "$PWD":/work -w /work alpine:3.20 \
    sh ci/build-musl.sh   # → build/linux/arm64/release/AbilityFramework (static)
```
