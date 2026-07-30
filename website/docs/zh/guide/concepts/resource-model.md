# 资源模型

AbilityFramework 的资源模型借鉴 Kubernetes 的 CRD/CR 模式，将"资源类型定义"与"资源实例声明"分离，并在此基础上引入了 Manifest（能力清单）和 Instance（运行时实例）两个概念。

## 核心概念一览

| 概念 | 类比 | 职责 | 维护方 |
|---|---|---|---|
| **CRD** | 类定义 / interface | 框架内置的资源类型 schema，定义 CR 的通用结构（kind 枚举、metadata 规范、spec 通用字段） | 框架 |
| **Manifest** | API 规范文档 | 能力清单，声明能力对外提供的服务、任务、配置 schema 和依赖 | 能力开发者 |
| **CR** | 实例声明 / object literal | 具体能力的实例声明，指定 package、version、config 等 | 能力开发者 |
| **Instance** | 运行时对象 / new 实例 | CR 模板在运行时派生的副本，拥有独立 instance_id 和冻结的 spec_snapshot | 框架运行时 |
| **Service CR** | 服务声明 | 与 Ability CR 并列，声明常驻运行的服务资源，支持重启策略和健康检查 | 能力开发者 |

## CRD（框架内置）

CRD（CustomResourceDefinition）是框架内置的资源类型定义。在 v3 中，CRD 从能力包中抽离为框架唯一内置定义（内嵌于二进制或从 `$ABILITY_FRAMEWORK_HOME/schema/ability.crd.yaml` 加载）。

关键字段：`kind` 枚举（`AtomAbility` / `ComposeAbility` / `AbstractAbility`）、`metadata`（name/labels/annotations）、`spec`（package/version/abilityName/position/autoStart/config/devices/models 等）。

## Manifest（能力清单）

Manifest 由能力开发者编写，随包发布。它描述能力的接口和行为：

- `provides` — 能力对外提供的服务描述
- `tasks` — 任务定义（参数 schema + 返回值）
- `schema` — config / status / debugOption 的 OpenAPI V3 Schema
- `depends` — 依赖声明（abilities / devices / models）

## CR（能力声明）

CR（Custom Resource）是具体能力的实例声明。它符合 CRD 的 schema 规范，并引用一个 Manifest 来描述能力接口。运行时不能直接创建/修改/删除 CR —— CR 由能力包内置的 `crs/*.yaml` 或工作目录 `crs/*.yaml` 决定。

## Instance（运行时实例）

Instance 是 CR 模板在运行时派生的副本。框架在创建实例时会冻结一份 `spec_snapshot`，即使模板之后被修改或删除，正在运行的实例仍按创建那一刻的 spec 工作。实例退出后立即删除，不保留历史。

> CR 模板像类（class），能力实例像对象（object）。`POST /api/instance` 相当于 `new`，实例退出相当于析构。

## Service CR

Service 类型 CR 与 Ability CR 并列。Ability 是按需调用的，Service 是常驻运行的。Service 支持 `restartPolicy`（always / on-failure / never）、`maxRestarts`、`healthCheck`（liveness + readiness 探活）等配置。

## 两级验证流程

1. **框架级验证** — CR 整体结构符合内置 CRD 的 schema
2. **能力级验证** — CR 的 config/debugOption/status 符合 Manifest 中的 schema

## 相关参考

- [架构演进设计](/guide/release-highlights/architecture-evolution) — CRD 内置化的背景与 Phase 1-3 演进规划
- [能力实例](/guide/release-highlights/instance) — Instance 的完整生命周期与数据模型
- [资源管理器](/guide/concepts/architecture) — CR/CRD/Instance 的实现细节
- [HTTP API 参考](/api/http-api) — Manifest / CR / Instance 相关 endpoint
