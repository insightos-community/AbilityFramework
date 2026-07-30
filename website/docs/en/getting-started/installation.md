# Installation

## Prerequisites

- Linux x86_64 (Ubuntu 20.04+)
- Python 3.8+ (uv recommended for managing virtual environments)
- git-lfs (`sudo apt install git-lfs`)

## Clone the project

```bash
git clone ssh://git@git.insightos.cn:32774/kernel/ability-framework/mcp-playground.git
cd mcp-playground

# The repository uses Git LFS for large files; after the first clone:
git lfs install   # only once
git lfs pull      # pull the binaries
```

## Create a Python environment

### Install uv (if you don't have it)

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
source ~/.bashrc   # or source ~/.zshrc
```

### Create the project environment

```bash
uv venv .venv --python 3.12
source .venv/bin/activate
```

## Install the SDK and Scaffold

```bash
uv pip install ability_py-0.4.0-py3-none-any.whl
uv pip install ability_scaffold-1.2.0-py3-none-any.whl
```

Verify the installation:

```bash
python -c "import ability_py; print('SDK version:', ability_py.__version__)"
# → SDK version: 0.4.0

ability-scaffold version
# → ability-scaffold 1.1.1
```

## Next steps

With the environment ready, you can:

- [Quick start](./quick-start) — start the framework, deploy and call an ability
- [First project](./first-project) — develop your own ability from scratch
