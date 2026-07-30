# SDK

AbilityFramework provides ability-development SDKs in multiple languages that encapsulate interface definitions, heartbeat management, and lifecycle callbacks.

## Documentation index

- [Python SDK](/en/api/SDK/python) — `ability-py-sdk`
- [C++ SDK](/en/api/SDK/c++) — `ability-sdk`
- [Go SDK](/en/api/SDK/go)

## Ability development workflow

1. Use openapi-tool to generate the CR file and Manifest from an OpenAPI definition
2. Use the scaffold tool to generate the ability project skeleton
3. Implement `on_start` initialization and the `execute` method of each task
4. Package and upload to the framework

See [Architecture evolution design - Phase 1.5](/en/guide/release-highlights/architecture-evolution#phase-15-ability-project-scaffold-tool).
