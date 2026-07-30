# 能力开发全流程

从零开始，完成一个机械臂控制能力的**定义 → 生成 → 实现 → 打包 → 部署 → 自然语言调用**全流程。

## 概览

```mermaid
flowchart LR
    A["阶段 1<br/>定义 Schema"] -->|openapi-tool| B["阶段 2<br/>生成工程"]
    B -->|ability-scaffold| C["阶段 3<br/>实现 + 打包"]
    C --> D["阶段 4<br/>部署到框架"]
    D --> E["阶段 5<br/>MCP 调用"]
```

## 教程结构

本教程分为以下步骤，建议按顺序阅读：

1. [环境安装](./installation) — 拉取项目代码、安装 Python 环境与 SDK
2. [快速开始](./quick-start) — 启动框架、上传能力包、调用任务
3. [配置参考](./configuration) — 框架 `config.yaml` 最小配置字段说明
4. [第一个项目](./first-project) — 从零开发一个完整能力，并接入 MCP 自然语言调用

## 前提条件

- Linux x86_64（Ubuntu 20.04+）
- Python 3.8+（推荐用 uv 管理虚拟环境）
- git-lfs（`sudo apt install git-lfs`）

## 仓库内容

```
mcp-playground/
├── AbilityFramework                        # 框架二进制 v2.5.0 (静态链接, 可直接运行)
├── openapi-tool-v1.0.0-linux-x86_64       # OpenAPI 工具集 (单文件可执行)
├── ability_py-0.4.0-py3-none-any.whl      # Python SDK (wheel)
├── ability_scaffold-1.2.0-py3-none-any.whl # 脚手架工具 (wheel)
├── ability-mcp-server.zip                  # MCP Server (用于 AI agent 调用能力)
├── phase-1/ ~ phase-4/                     # 每个阶段的中间产物 (可直接参考)
└── README.md                               # 本教程
```

## 完成后的目录结构

完成全部步骤后的目录：

```
mcp-playground/
├── AbilityFramework                          # 框架二进制
├── openapi-tool-v1.0.0-linux-x86_64         # OpenAPI 工具
├── ability_py-0.4.0-py3-none-any.whl        # SDK wheel
├── ability_scaffold-1.2.0-py3-none-any.whl  # Scaffold wheel
├── MockArm.openapi.yaml                      # Phase 1 产物
├── mock-arm/                                 # Phase 2-3 产物 (能力工程)
│   ├── main.py
│   ├── task.py                               # ← 你填充的业务代码
│   ├── ability.manifest.yaml
│   ├── package.yaml
│   ├── requirements.txt
│   ├── bin/ability
│   ├── crs/mockarm-demo-1.cr.yaml
│   ├── skills/SKILL.md                       # ← 你编写的 Skill
│   └── service/{__init__.py, ability.py}
├── mock.arm.demo-1.0.0.zip                  # Phase 3 产物 (能力包)
├── framework/                                # Phase 4 运行时
│   ├── AbilityFramework
│   ├── config.yaml
│   ├── packages/mock.arm.demo/1.0.0/...     # 上传后自动解包
│   ├── crs/_packages/...                     # 自动镜像
│   ├── skills/_packages/...                  # 自动镜像
│   └── databases/...                         # SQLite 数据
├── ability-mcp-server.zip                   # MCP Server 源码
├── ability-mcp-server/                       # Phase 5 解压后
│   ├── .venv/                                # MCP Server 的 Python 环境
│   └── src/ability_mcp/native/               # 服务器代码
├── .venv/                                    # 能力开发 Python 虚拟环境
└── README.md                                 # 本教程
```

## 下一步

- 把 `task.py` 中的 mock 实现替换成真实机械臂 SDK 调用
- 把 `skills/SKILL.md` 改成你的领域知识
- 在 OpenClaw 中体验完整的自然语言 → 能力调用链路
- 查看 [ability-mcp-server 文档](https://git.insightos.cn:40080/kernel/ability-framework/ability-mcp-server) 了解更多工具和配置选项
