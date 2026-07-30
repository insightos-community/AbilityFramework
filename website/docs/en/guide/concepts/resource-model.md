# Resource model

The AbilityFramework resource model borrows Kubernetes' CRD/CR pattern, separating "resource type definitions" from "resource instance declarations", and adds two concepts on top: Manifest (the ability manifest) and Instance (a runtime instance).

## Core concepts

| Concept | Analogy | Responsibility | Maintained by |
|---|---|---|---|
| **CRD** | Class definition / interface | A framework-built-in resource type schema that defines the general structure of a CR (kind enum, metadata spec, common spec fields) | Framework |
| **Manifest** | API spec document | The ability manifest, declaring the services, tasks, configuration schema, and dependencies the ability provides | Ability developer |
| **CR** | Instance declaration / object literal | The instance declaration of a specific ability, specifying package, version, config, etc. | Ability developer |
| **Instance** | Runtime object / `new` instance | A copy derived from a CR template at runtime, with its own instance_id and a frozen spec_snapshot | Framework runtime |
| **Service CR** | Service declaration | Alongside Ability CRs, declares long-running service resources, supporting restart policies and health checks | Ability developer |

## CRD (framework built-in)

A CRD (CustomResourceDefinition) is a framework-built-in resource type definition. In v3, CRDs are extracted from ability packages into a single framework-built-in definition (embedded in the binary or loaded from `$ABILITY_FRAMEWORK_HOME/schema/ability.crd.yaml`).

Key fields: the `kind` enum (`AtomAbility` / `ComposeAbility` / `AbstractAbility`), `metadata` (name/labels/annotations), and `spec` (package/version/abilityName/position/autoStart/config/devices/models, etc.).

## Manifest (ability manifest)

The Manifest is written by the ability developer and published with the package. It describes the ability's interface and behavior:

- `provides` — descriptions of the services the ability exposes
- `tasks` — task definitions (parameter schema + return values)
- `schema` — OpenAPI V3 schemas for config / status / debugOption
- `depends` — dependency declarations (abilities / devices / models)

## CR (ability declaration)

A CR (Custom Resource) is the instance declaration of a specific ability. It conforms to the CRD schema and references a Manifest that describes the ability interface. At runtime you cannot directly create/modify/delete a CR — a CR is determined by the `crs/*.yaml` embedded in the ability package or by the `crs/*.yaml` in the working directory.

## Instance (runtime instance)

An Instance is a copy derived from a CR template at runtime. When the framework creates an instance, it freezes a `spec_snapshot`, so that even if the template is later modified or deleted, the running instance keeps working with the spec from the moment of creation. The instance is deleted immediately upon exit and is not retained in history.

> A CR template is like a class, and an ability instance is like an object. `POST /api/instance` is like `new`, and instance exit is like destruction.

## Service CR

A Service CR sits alongside an Ability CR. An Ability is invoked on demand; a Service runs persistently. Service supports configuration such as `restartPolicy` (always / on-failure / never), `maxRestarts`, and `healthCheck` (liveness + readiness probes).

## Two-level validation flow

1. **Framework-level validation** — the overall CR structure conforms to the built-in CRD schema
2. **Ability-level validation** — the CR's config/debugOption/status conform to the schemas in the Manifest

## Related references

- [Architecture evolution design](/en/guide/release-highlights/architecture-evolution) — background and the Phase 1-3 evolution plan for CRD internalization
- [Ability instances](/en/guide/release-highlights/instance) — the full lifecycle and data model of an Instance
- [Resource manager](/en/guide/concepts/architecture) — implementation details of CR/CRD/Instance
- [HTTP API reference](/en/api/http-api) — Manifest / CR / Instance endpoints
