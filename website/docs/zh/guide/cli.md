# 命令

> 二进制名: **AbilityFramework** &nbsp;|&nbsp; 版本: 由 `git describe --tags` 推导 &nbsp;|&nbsp; 帮助: `./AbilityFramework -h`

AbilityFramework 是一个单二进制命令行程序，集成了配置导出、Schema 导出、启动运行三种主要工作模式。所有操作通过命令行参数驱动，无交互式子命令。

---

## 用法

```text
AbilityFramework [选项]
```

如果只传信息类选项（`-h` / `-v` / `-d` / `--dump-schemas` / `-o`），程序输出结果后立即退出，不启动服务。只有传入 `-c`（或不传参数）时才会进入服务运行模式。

## 选项一览

| 选项 | 简写 | 接受参数 | 说明 |
|---|---|---|---|
| `--help` | `-h` | 无 | 显示帮助信息并退出。 |
| `--version` | `-v` | 无 | 显示版本信息并退出。 |
| `--config <file>` | `-c` | 文件名或路径 | 指定配置文件。未传时读取 `<home>/config.yaml`。 |
| `--dump` | `-d` | 无 | 输出默认配置到终端并退出。 |
| `--dump-schemas` | — | 无 | 输出 package/manifest/CR 的 JSON Schema 到终端并退出。 |
| `--output <file>` | `-o` | 文件名 | 将默认配置写入指定文件并退出。 |
| `--full` | `-f` | 无 | 与 `-d` / `-o` 配合使用，输出包含所有可选项的完整配置。 |
| `--verbose <int>` | `-V` | 整数（默认 `0`） | 设置日志冗余级别，数值越大输出越详细。 |
| `--http-server-threads <int>` | — | 整数（默认 `0`） | 设置 HTTP 服务线程池大小，`0` 表示自动配置。 |
| `--log-to-stdout` | — | 无 | 将日志同时输出到标准输出。 |

---

## 命令示例

### 查看帮助与版本

```bash
./AbilityFramework -h        # 显示帮助
./AbilityFramework -v        # 显示版本信息
```

版本信息格式：`AbilityFramework <VERSION> (git commit <HASH>, built on <DATE>)`，其中 `VERSION` 来自 `git describe --tags`。

### 导出默认配置

```bash
# 输出基础配置到终端
./AbilityFramework -d

# 输出完整配置（含 discovery 组网、election、opentelemetry 等可选项）到终端
./AbilityFramework -df

# 写入基础配置模板到文件
./AbilityFramework -o config.yaml

# 写入完整配置模板到文件
./AbilityFramework -of config.yaml
```

基础配置与完整配置的差异在于：完整配置额外包含 `discovery_mgr.teams`、`discovery_mgr.election`、`source_urls`、`opentelemetry`、`webui.custom_path` 等高级可选项。详见[配置文件参考](/guide/configuration/config-file)。

### 导出 JSON Schema

```bash
# 输出 package / manifest / CR 的 JSON Schema（供 Studio 表单或脚手架工具使用）
./AbilityFramework --dump-schemas
```

该命令输出后立即退出，不初始化日志、数据库或事件循环。

### 启动服务

```bash
# 使用默认 config.yaml（位于 ABILITY_FRAMEWORK_HOME 或当前目录）
./AbilityFramework

# 指定配置文件启动
./AbilityFramework -c config.yaml

# 指定配置文件 + 详细日志 + 日志同步输出到 stdout
./AbilityFramework -c config.yaml --verbose 2 --log-to-stdout
```

启动成功后，控制台会打印服务地址：

```text

  AbilityFramework is running

  API:   http://localhost:8080/api/hello
  WebUI: http://localhost:8080/ui
  Log:   <日志输出描述>
```

按 `Ctrl+C`（SIGINT）触发优雅关闭：停止定时任务、关闭 HTTP 服务、关闭各模块、关闭数据库后退出。

### 工作目录与环境变量

框架的工作主目录由环境变量 `ABILITY_FRAMEWORK_HOME` 决定，未设置时使用当前工作目录：

```bash
# 设置工作主目录后启动
export ABILITY_FRAMEWORK_HOME=/opt/ability-framework
./AbilityFramework -c config.yaml
```

日志、能力包、数据库等运行时文件均存放在该目录下，详见[配置文件参考 — 启动时自动创建的目录](/guide/configuration/config-file#启动时自动创建的目录)。

---

## 选项详细说明

### `-c, --config <file>`

指定配置文件。参数可以是纯文件名或路径：

- **纯文件名**（不含 `/`，如 `config.yaml`）：解析为 `<ABILITY_FRAMEWORK_HOME>/<文件名>`。
- **路径**（绝对路径，或含 `./`、`../`、`/`、`\` 等）：按原路径使用。

未传 `-c` 时默认读取 `<home>/config.yaml`。

### `-d, --dump`

将默认配置模板输出到标准输出。默认输出的是基础配置。配合 `-f` 输出完整配置。输出后程序退出。

### `-f, --full`

修饰 `-d` / `-o`，使其输出/写入**完整**配置模板（包含组网、选举、链路追踪等可选项）。单独使用无效。

### `-o, --output <file>`

将默认配置模板写入指定文件。写入成功后输出 `默认配置已写入: <file>` 并退出。配合 `-f` 写入完整配置。

### `--dump-schemas`

输出 package / manifest / CR 的 JSON Schema 到标准输出，供 Studio 表单驱动或脚手架工具消费。输出后程序退出，不启动任何运行时组件。

### `-V, --verbose <int>`

设置日志冗余级别（VLOG 级别），默认 `0`。数值越大，输出的调试日志越多：

- `> 1`：insightos-log 后端会将级别下调到 `TRACE`；
- `> 0`：下调到 `DEBUG`。

### `--http-server-threads <int>`

设置 HTTP 服务线程池大小，默认 `0` 表示由 httplib 自动配置。设置为正整数时使用固定线程池。

### `--log-to-stdout`

强制将日志同时输出到标准输出。对 glog 后端等价于设置 `also_log_to_stderr`；对 insightos-log 后端，若当前为纯文件输出则会切换到 `dual`（控制台 + 文件）。

---

## 退出码

| 退出码 | 含义 |
|---|---|
| `0` | 正常退出（帮助/版本/导出成功、收到 SIGINT 优雅关闭）。 |
| `1` | 参数解析错误或配置写入失败。 |
| `-1`（255） | 启动失败，常见原因：HTTP 端口被占用、目录创建失败、HTTP 绑定失败。 |
