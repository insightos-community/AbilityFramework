# SDK

AbilityFramework 提供多种语言的能力开发 SDK，封装接口定义、心跳管理和生命周期回调。

## 文档索引

- [Python SDK](/api/SDK/python) — `ability-py-sdk`
- [C++ SDK](/api/SDK/c++) — `ability-sdk`
- [Go SDK](/api/SDK/go)

## 能力开发流程

1. 使用 openapi-tool 从 OpenAPI 定义生成 CR 文件和 Manifest
2. 使用脚手架工具生成能力工程骨架
3. 实现 `on_start` 初始化和各 task 的 `execute` 方法
4. 打包上传到框架

详见 [架构演进设计 - Phase 1.5](/guide/release-highlights/architecture-evolution#phase-15能力工程脚手架工具)。
