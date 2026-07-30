# Logging

::: info
Related configuration: the `log` option in `config.yaml`
:::

The logging system is one of AbilityFramework's core infrastructure pieces. It uses a **unified logging layer** to hide the underlying backend differences, so business code does not care whether the current backend is Google glog or the insightos-log SDK, while remaining compatible with both the legacy glog streaming style and the fmt style preferred by new code. The core design is the **strategy pattern + a singleton manager**: one `ILogger` interface, two implementations, a factory plus a state container.

## Architecture overview

```mermaid
C4Context
    title The logging system within the framework

    Person(dev, "Developer", "writes LOG_INFO / LOG(INFO)")
    Person(ops, "Ops", "edits the config.yaml log node")

    System_Boundary(fwk, "AbilityFramework") {
        System_Boundary(log, "logger unified layer") {
            System(mac, "macros and facade", "LOG_INFO / LOG / VLOG / CHECK")
            System(mgr, "LoggerManager", "factory + singleton state")
            System( iface, "ILogger interface", "Trace/Info/Warn/...")
        }
        System(api, "HTTP Server", "tracing middleware")
    }

    System_Ext(glog, "GlogStrategy", "Google glog text logs")
    System_Ext(insight, "InsightosLogStrategy", "insightos-log structured SDK")
    System_Ext(file, "log files", "log/ or logs/")

    Rel(dev, mac, "call logging macros")
    Rel(mac, mgr, "forward")
    Rel(mgr, iface, "holds current strategy")
    Rel(iface, glog, "Glog backend implementation")
    Rel(iface, insight, "InsightosLog backend implementation")
    Rel(glog, file, "write files")
    Rel(insight, file, "write files + stdout")
    Rel(ops, mgr, "config.yaml selects backend")
    Rel(api, mgr, "trace_id / span_id chains")
```

---

## Strategy pattern and class structure

### Backend selection

The `logger::Backend` enum exposes only the backend kind without binding to a specific third-party library type, so that adding a third logging implementation requires no changes to callers or most of the initialization logic.

| Backend | Strategy class | Characteristics |
|---|---|---|
| `Glog` | `GlogStrategy` | Process-level text logger, compatible with legacy glog calls |
| `InsightosLog` | `InsightosLogStrategy` | Structured logging SDK, supports service metadata and tracing |

```mermaid
classDiagram
    class ILogger {
        <<interface>>
        +backend() Backend
        +backend_name() string
        +log_path() path
        +Trace(message, caller)
        +Debug(message, caller)
        +Info(message, caller)
        +Warn(message, caller)
        +Error(message, caller)
        +Fatal(message, caller)
        +Flush()
        +Shutdown()
    }

    class GlogStrategy {
        -GlogConfig config_
        +Write(severity, prefix, message, caller)
    }

    class InsightosLogStrategy {
        -InsightosLogConfig config_
        -path log_path_
        -path current_log_file_
        -string output_description_
    }

    class LoggerManager {
        <<singleton>>
        +InitFromConfig(path, options)
        +ParseConfigFile(path, options) LoggerConfig
        +SetVerbosity(level)
        +StartNewTrace()
        +ContinueTrace(trace_id, span_id)
        +Instance() ILogger
        +CurrentBackend() Backend
    }

    ILogger <|.. GlogStrategy : implements
    ILogger <|.. InsightosLogStrategy : implements
    LoggerManager o--> ILogger : holds current strategy
```

### Why the two backends cannot share one set of fields

The models of glog and insightos-log do not align: glog is more like a "process-level text logger", while insightos-log is a "structured logging SDK". So the configuration is modeled separately first (`GlogConfig` and `InsightosLogConfig`), and then an exclusive selection is made through `LoggerConfig`, avoiding forcing the two backends into a single set of fields. `LoggerManager` only needs to hold the one decision result — "the currently selected backend + that backend's config" — to rebuild or switch the strategy object at runtime.

---

## Three call styles

Regardless of which style business code uses, it ultimately goes through the same strategy dispatch path (`detail::write_immediate_log`).

```mermaid
flowchart TD
    subgraph style1["Style 1: fmt style (recommended for new code)"]
        F1["LOG_INFO(id={}, id)"] --> F2["Logger::Info(caller, fmt)"]
    end

    subgraph style2["Style 2: streaming style (legacy compat)"]
        S1["LOG(INFO) << a << b"] --> S2["make_log_stream"]
        S2 --> S3["LogStream joins on destruction"]
    end

    subgraph style3["Style 3: assertion macros"]
        C1["CHECK(cond) << detail"] --> C2["make_check_stream"]
        C2 --> C3{"cond is false?"}
        C3 -- yes --> C4["CheckStream destruction\nwrite FATAL + abort"]
        C3 -- no --> C5["no-op"]
    end

    F2 --> W["detail::write_immediate_log\nseverity + message + caller"]
    S3 --> W

    W --> EMIT["emit_log"]
    EMIT --> INIT{"LoggerManager\ninitialized?"}
    INIT -- yes --> DISP["dispatch_to_strategy\nforward to current strategy by severity"]
    INIT -- no --> FB["write_fallback_log\ndegrade to stderr"]
    DISP --> FILE["flush to file / stdout"]

    EMIT --> FATAL{"severity == FATAL?"}
    FATAL -- yes --> ABORT["std::abort()"]
    FATAL -- no --> END["end"]
```

### Style 1: fmt formatting macros (recommended)

New code should prefer the fmt style and avoid piling on more `<<`:

```cpp
LOG_TRACE("trace detail {}", ctx);
LOG_DEBUG("debug id={}", id);
LOG_INFO("server started on {}:{}", ip, port);
LOG_WARN("retry {}/{}", attempt, max);
LOG_ERROR("failed to parse {}", path);
LOG_FATAL("unreachable");
```

When the macros expand, they capture call-site information via `__FILE__` / `__LINE__` / `__PRETTY_FUNCTION__`, then hand it to the `Logger` facade to format and dispatch. This way both backends receive unified caller metadata, lowering the cost of locating logs during retrieval.

### Style 2: streaming macros (compatible with glog style)

Legacy code heavily uses `LOG(INFO) << a << b`. Hand-migrating all of it to the fmt style is costly and error-prone, so the unified layer uses a temporary stream object `LogStream` to first assemble the text and then commit it uniformly on destruction, achieving "compatible call form, unified backend implementation":

```cpp
LOG(INFO) << "create ability instance " << id << " from " << name;
LOG(WARNING) << "dropping orphan heartbeat from " << id_str;
```

`VLOG(n)` maps to `StreamSeverity::DEBUG` and is gated by `LoggerManager::IsVerboseEnabled(n)`. `LOG_IF(level, cond)`, `LOG_FIRST_N(level, count)`, and `DLOG(level)` are likewise redirected to the unified layer.

### Style 3: CHECK assertions

`CheckStream` reuses the "commit on destruction" principle, but only outputs a log and aborts the process when the assertion fails, without depending on whether a particular glog distribution provides a complete CHECK macro family:

```cpp
CHECK(ptr != nullptr);
CHECK_NOTNULL(ptr);
CHECK_EQ(a, b) << "a must equal b";
CHECK_NE(a, b);
CHECK_GE(version, 2);
```

Even when business logging switches to insightos-log, the framework still initializes a minimal glog environment in compatibility mode (writing logs to `<home>/log/legacy-glog`) specifically to support assertion macros such as `CHECK`.

---

## Configuration parsing

`LoggerManager::ParseConfigFile` reads the `log` node of config.yaml, performs exclusive validation, and produces a `LoggerConfig`.

### Exclusive validation

`log` is a map that **must and can only** contain exactly one of `glog` or `insightos-log`. Having both or neither throws an exception and causes startup failure.

```mermaid
flowchart TD
    START["ParseConfigFile"] --> EXIST{"config file exists?"}
    EXIST -- no --> ERR1["throw: file does not exist"]
    EXIST -- yes --> LOAD["YAML::LoadFile"]
    LOAD --> NODE{"log node exists and is a map?"}
    NODE -- no --> ERR2["throw: log must exist and be a map"]
    NODE -- yes --> COUNT{"exactly one of\nglog or insightos-log?"}
    COUNT -- both / neither --> ERR3["throw: exactly one of glog or insightos-log"]
    COUNT -- glog only --> PARSE_G["parse GlogConfig"]
    COUNT -- insightos-log only --> PARSE_I["parse InsightosLogConfig"]
    PARSE_G --> SET_G["backend = Glog"]
    PARSE_I --> SET_I["backend = InsightosLog"]
    SET_G --> RET["return LoggerConfig"]
    SET_I --> SN{"service_name non-empty?"}
    SN -- no --> ERR4["throw: service_name must be non-empty"]
    SN -- yes --> RET
```

### Path resolution

Relative paths are resolved against workspace_root (i.e. `ABILITY_FRAMEWORK_HOME` or the process working directory). When insightos-log's `log_root` is empty it falls back to `$HOME/insightoslog`, or `/tmp/insightoslog` if `$HOME` does not exist. The actual landing directory also appends `service_name` and `instance_id`.

### Backend selection flow

```mermaid
flowchart TD
    CFG["LoggerConfig"] --> FACT["make_strategy(config)"]
    FACT --> BE{"config.backend"}
    BE -- Glog --> NEW_G["new GlogStrategy(glog_config)"]
    BE -- InsightosLog --> NEW_I["new InsightosLogStrategy(insightos_config)"]
    NEW_G --> STRAT["unique_ptr~ILogger~"]
    NEW_I --> STRAT
```

### Configuration examples

glog backend:

```yaml
log:
  glog:
    color_log: true
    also_log_to_stderr: false
    max_log_size: 1024
    stop_logging_if_full_disk: true
    log_dir: log
```

insightos-log backend (the default configuration in config.yaml):

```yaml
log:
  insightos-log:
    level: INFO
    service_type: ability
    service_name: AbilityFramework
    instance_id: af
    output: dual
    log_root: logs
    buffer_size: 8192
    flush_interval: 2
    enable_tracing: true
    file:
      max_size: 1048576
      max_files: 3
```

For the full field reference, see [Configuration file reference - the log node](/en/guide/configuration/config-file#log-logging-configuration).

---

## Initialization flow

The logging system is initialized **earliest** in `src/main.cpp`, before module construction and database initialization. If the backend is not decided here first, a split state would emerge at runtime where "some logs go to glog, others go to the new interface".

```mermaid
sequenceDiagram
    participant Main as main.cpp
    participant Mgr as LoggerManager
    participant Cfg as ParseConfigFile
    participant Strat as make_strategy
    participant Glog as glog compatibility layer

    Main->>Mgr: InitFromConfig(config_path, options)
    Mgr->>Cfg: ParseConfigFile
    Cfg-->>Mgr: LoggerConfig

    alt backend != Glog and compatibility enabled
        Mgr->>Glog: apply_glog_config(legacy-glog dir)
        Note over Glog: minimal glog env to support CHECK macros
    end

    Mgr->>Strat: make_strategy(config)
    Strat-->>Mgr: strategy object
    Mgr->>Mgr: hold logger + active_config
    Mgr->>Mgr: apply_runtime_verbosity

    Main->>Mgr: SetVerbosity(options.verbose_log)
    Note over Mgr: from the --verbose-log CLI argument
```

### `LoggerInitOptions`

Runtime options applied during initialization:

| Field | Default | Description |
|---|---|---|
| `workspace_root` | empty | The base directory for resolving relative paths |
| `force_console_output` | false | Corresponds to the `--log-to-stdout` CLI argument; forces console output |
| `enable_legacy_glog_compatibility` | true | Keeps a minimal glog environment for CHECK/legacy macros during migration |

---

## Verbosity control

The `--verbose-log N` CLI argument takes effect via `LoggerManager::SetVerbosity(N)`. It affects two layers simultaneously:

1. The `VLOG(n)` switch: `IsVerboseEnabled(n)` checks `n <= verbosity_level`;
2. glog's `SetVLOGLevel("*", N)`;
3. the insightos-log backend's SDK minimum level (verbosity > 1 lowers to TRACE, > 0 lowers to DEBUG).

```mermaid
flowchart TD
    CLI["--verbose-log N"] --> SET["SetVerbosity(N)"]
    SET --> STORE["verbosity_level = max(N, 0)"]
    STORE --> APPLY["apply_runtime_verbosity"]

    APPLY --> GV["google::SetVLOGLevel(*, N)"]

    APPLY --> BE{"current backend?"}
    BE -- InsightosLog --> DESIRED{"N value"}
    DESIRED -- "> 1" --> TR["SetLevel(TRACE)"]
    DESIRED -- "> 0" --> DE["SetLevel(DEBUG)"]
    DESIRED -- "0" --> KEEP["keep config.level"]
    BE -- Glog --> DONE["only affects the VLOG switch"]
```

---

## Tracing

When the backend is insightos-log and `enable_tracing: true`, the framework maintains a trace chain for each HTTP request and returns `trace_id` / `span_id` to the caller via response headers. All trace methods are no-ops when the backend is Glog, so callers need not check the backend type.

### HTTP middleware

`src/main.cpp` registers two middlewares:

- `set_pre_routing_handler` (request entry): checks the `X-InsightOSLog-TraceID` header; if present it continues the chain, otherwise it starts a new chain;
- `set_post_routing_handler` (response exit): writes the current `trace_id` and `span_id` into the response headers.

```mermaid
sequenceDiagram
    participant Caller as Caller
    participant HTTP as HTTP Server
    participant Pre as pre_routing_handler
    participant Mgr as LoggerManager
    participant Handler as business route
    participant Post as post_routing_handler

    Caller->>HTTP: request (may carry X-InsightOSLog-TraceID)
    HTTP->>Pre: pre_routing

    alt has TraceID header
        Pre->>Mgr: ContinueTrace(trace_id, span_id)
    else no TraceID header
        Pre->>Mgr: StartNewTrace()
        Mgr-->>Pre: generate new trace_id + span_id
    end

    Pre-->>HTTP: Unhandled (continue)
    HTTP->>Handler: business handling (logs carry trace context)
    Handler-->>HTTP: response
    HTTP->>Post: post_routing
    Post->>Mgr: GetTraceID() / GetSpanID()
    Post-->>HTTP: inject response headers
    HTTP-->>Caller: response + X-InsightOSLog-TraceID/SpanID
```

### Trace method reference

| Method | Description | Glog backend behavior |
|---|---|---|
| `StartNewTrace()` | Starts a new trace chain for the current thread | no-op |
| `ContinueTrace(trace_id, span_id)` | Continues an existing chain | no-op |
| `GenerateSpanID()` | Generates a new span id | returns empty string |
| `GetTraceID()` | Gets the current trace id | returns empty string |
| `GetSpanID()` | Gets the current span id | returns empty string |

After the caller gets the trace_id from the response header, it can correlate all logs of the same request in the logging system to form a complete call-chain view.

---

## Environment variable interactions

| Environment variable | Description |
|---|---|
| `ABILITY_FRAMEWORK_HOME` | The framework's home directory; relative log paths are resolved against it |
| `LOG_LEVEL` | Automatically set and injected into subprocesses when using the insightos-log backend, keeping their log level consistent with the main process |
| `HOME` | The fallback base directory when insightos-log's `log_root` is empty |

The `--log-to-stdout` CLI argument forces `force_console_output = true`: the glog backend enables `also_log_to_stderr`, and the insightos-log backend switches from `rotate_file` to `dual` if so configured.

---

## Runtime control interface

```mermaid
flowchart TD
    subgraph lifecycle
        I["InitFromConfig"] --> U["IsInitialized"]
        U --> S["Shutdown"]
    end

    subgraph runtime queries
        CB["CurrentBackend"]
        BN["BackendName"]
        ALP["ActiveLogPath"]
        ALF["ActiveLogFile"]
        DO["DescribeOutput"]
    end

    subgraph dynamic control
        SV["SetVerbosity(N)"]
        GV2["GetVerbosity"]
        IVE["IsVerboseEnabled(n)"]
    end

    subgraph tracing
        SN["StartNewTrace"]
        CT["ContinueTrace"]
        GT["GetTraceID"]
        GS["GetSpanID"]
    end
```

`LoggerManager` is essentially a factory + singleton state container: it parses the configuration and validates exclusivity, creates and holds the current strategy object, and exposes runtime information such as the current backend, output path, and verbosity. All static methods are thread-safe through the internal `LoggerState` (a mutex-protected `unique_ptr<ILogger>` + `LoggerConfig` + atomic verbosity).

### FATAL semantics

After a log is flushed, `emit_log` calls `std::abort()` to terminate the process if the severity is `FATAL`, preserving the glog-era "fatal log means termination" semantics. `CHECK` assertion failures also go through this path.

---

## Related references

- [Configuration file reference](/en/guide/configuration/config-file)
- [CLI command reference](/en/guide/cli)
- [Skill document system](/en/guide/release-highlights/skill)
- [Ability instances](/en/guide/release-highlights/instance)
