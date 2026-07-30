# 环境变量

AbilityFramework 通过环境变量控制框架的工作目录与子进程行为。

| 变量 | 说明 |
|---|---|
| `ABILITY_FRAMEWORK_HOME` | 框架工作主目录。未设置时使用进程当前工作目录。日志、能力包、数据库等运行时文件均存放在该目录下。 |
| `LOG_LEVEL` | 框架使用 `insightos-log` 后端时会自动设置该变量并注入子进程，保证子进程日志级别与主进程一致。 |

## 与命令行参数的联动

`--log-to-stdout` 命令行参数会强制 `force_console_output = true`：glog 后端开启 `also_log_to_stderr`，insightos-log 后端若配置为纯文件输出则改为 `dual`（控制台 + 文件）。

## 相关参考

- [配置文件参考](/guide/configuration/config-file)
- [CLI 命令参考](/guide/cli)
- [日志系统](/guide/release-highlights/log)
