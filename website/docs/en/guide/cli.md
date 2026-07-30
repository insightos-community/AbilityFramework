# Commands

> Binary name: **AbilityFramework** &nbsp;|&nbsp; Version: derived from `git describe --tags` &nbsp;|&nbsp; Help: `./AbilityFramework -h`

AbilityFramework is a single-binary command-line program that integrates three main working modes: config export, schema export, and service startup. All operations are driven by command-line arguments; there are no interactive subcommands.

---

## Usage

```text
AbilityFramework [options]
```

If only informational options are passed (`-h` / `-v` / `-d` / `--dump-schemas` / `-o`), the program prints the result and exits immediately without starting the service. Only when `-c` is passed (or no arguments at all) does it enter service mode.

## Options overview

| Option | Short | Accepts | Description |
|---|---|---|---|
| `--help` | `-h` | none | Show help and exit. |
| `--version` | `-v` | none | Show version information and exit. |
| `--config <file>` | `-c` | filename or path | Specify the configuration file. When omitted, `<home>/config.yaml` is read. |
| `--dump` | `-d` | none | Print the default configuration to the terminal and exit. |
| `--dump-schemas` | — | none | Print the JSON Schemas for package/manifest/CR to the terminal and exit. |
| `--output <file>` | `-o` | filename | Write the default configuration to the given file and exit. |
| `--full` | `-f` | none | Use with `-d` / `-o` to output the complete configuration including all optional fields. |
| `--verbose <int>` | `-V` | integer (default `0`) | Set the log verbosity level; higher values produce more detail. |
| `--http-server-threads <int>` | — | integer (default `0`) | Set the HTTP service thread pool size; `0` means auto. |
| `--log-to-stdout` | — | none | Also write logs to standard output. |

---

## Command examples

### View help and version

```bash
./AbilityFramework -h        # show help
./AbilityFramework -v        # show version
```

Version format: `AbilityFramework <VERSION> (git commit <HASH>, built on <DATE>)`, where `VERSION` comes from `git describe --tags`.

### Export the default configuration

```bash
# Print the base configuration to the terminal
./AbilityFramework -d

# Print the full configuration (incl. discovery, election, opentelemetry, etc.) to the terminal
./AbilityFramework -df

# Write the base configuration template to a file
./AbilityFramework -o config.yaml

# Write the full configuration template to a file
./AbilityFramework -of config.yaml
```

The difference between base and full configuration is that the full configuration additionally includes advanced options such as `discovery_mgr.teams`, `discovery_mgr.election`, `source_urls`, `opentelemetry`, and `webui.custom_path`. See the [Configuration file reference](/en/guide/configuration/config-file).

### Export JSON Schemas

```bash
# Print the JSON Schemas for package / manifest / CR (used by the Studio form or scaffold tools)
./AbilityFramework --dump-schemas
```

This command exits immediately after printing; it does not initialize logging, the database, or the event loop.

### Start the service

```bash
# Use the default config.yaml (in ABILITY_FRAMEWORK_HOME or the current directory)
./AbilityFramework

# Start with a specified configuration file
./AbilityFramework -c config.yaml

# Specify a config file + verbose logs + mirror logs to stdout
./AbilityFramework -c config.yaml --verbose 2 --log-to-stdout
```

On a successful startup, the console prints the service addresses:

```text

  AbilityFramework is running

  API:   http://localhost:8080/api/hello
  WebUI: http://localhost:8080/ui
  Log:   <log output description>
```

Press `Ctrl+C` (SIGINT) to trigger a graceful shutdown: stop scheduled tasks, close the HTTP service, shut down each module, close the database, then exit.

### Working directory and environment variables

The framework's home directory is determined by the `ABILITY_FRAMEWORK_HOME` environment variable; when unset, the current working directory is used:

```bash
# Set the home directory and start
export ABILITY_FRAMEWORK_HOME=/opt/ability-framework
./AbilityFramework -c config.yaml
```

Logs, ability packages, databases, and other runtime files are stored under this directory. See [Configuration file reference — directories created on startup](/en/guide/configuration/config-file#directories-created-on-startup).

---

## Option details

### `-c, --config <file>`

Specify the configuration file. The argument can be a plain filename or a path:

- **Plain filename** (no `/`, e.g. `config.yaml`): resolved to `<ABILITY_FRAMEWORK_HOME>/<filename>`.
- **Path** (absolute, or containing `./`, `../`, `/`, `\`): used as-is.

When `-c` is omitted, `<home>/config.yaml` is read by default.

### `-d, --dump`

Print the default configuration template to standard output. The default output is the base configuration. Combine with `-f` to output the full configuration. The program exits after printing.

### `-f, --full`

Modifies `-d` / `-o` to output/write the **full** configuration template (including optional sections such as networking, election, and tracing). Has no effect on its own.

### `-o, --output <file>`

Write the default configuration template to the given file. On success it prints `Default configuration written to: <file>` and exits. Combine with `-f` to write the full configuration.

### `--dump-schemas`

Print the JSON Schemas for package / manifest / CR to standard output, for consumption by the Studio form or scaffold tools. The program exits after printing without starting any runtime component.

### `-V, --verbose <int>`

Set the log verbosity level (VLOG level), default `0`. Higher values produce more debug logs:

- `> 1`: the insightos-log backend lowers the level to `TRACE`;
- `> 0`: lowers the level to `DEBUG`.

### `--http-server-threads <int>`

Set the HTTP service thread pool size; `0` (default) means httplib auto-configures it. A positive integer uses a fixed thread pool.

### `--log-to-stdout`

Force logs to also be written to standard output. For the glog backend this is equivalent to enabling `also_log_to_stderr`; for the insightos-log backend, if it was pure-file output it switches to `dual` (console + file).

---

## Exit codes

| Exit code | Meaning |
|---|---|
| `0` | Normal exit (help/version/dump succeeded, or graceful shutdown on SIGINT). |
| `1` | Argument parsing error or configuration write failure. |
| `-1` (255) | Startup failure, common causes: the HTTP port is in use, directory creation failed, or HTTP binding failed. |
