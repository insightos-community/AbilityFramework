# 环境安装

## 前提条件

- Linux x86_64（Ubuntu 20.04+）
- Python 3.8+（推荐用 uv 管理虚拟环境）
- git-lfs（`sudo apt install git-lfs`）

## 拉取项目

```bash
git clone ssh://git@git.insightos.cn:32774/kernel/ability-framework/mcp-playground.git
cd mcp-playground

# 仓库使用 Git LFS 管理大文件, 首次 clone 后需要:
git lfs install   # 仅首次
git lfs pull      # 拉取二进制
```

## 创建 Python 环境

### 安装 uv（如果没有）

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
source ~/.bashrc   # 或 source ~/.zshrc
```

### 创建项目环境

```bash
uv venv .venv --python 3.12
source .venv/bin/activate
```

## 安装 SDK 和 Scaffold

```bash
uv pip install ability_py-0.4.0-py3-none-any.whl
uv pip install ability_scaffold-1.2.0-py3-none-any.whl
```

验证安装：

```bash
python -c "import ability_py; print('SDK version:', ability_py.__version__)"
# → SDK version: 0.4.0

ability-scaffold version
# → ability-scaffold 1.1.1
```

## 下一步

环境就绪后，可以：

- [快速开始](./quick-start) — 启动框架、部署并调用能力
- [第一个项目](./first-project) — 从零开发你自己的能力
