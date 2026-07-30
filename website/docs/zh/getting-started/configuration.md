# 配置参考

AbilityFramework 启动时读取工作目录下的 `config.yaml`。以下是教程中使用的最小配置：

```yaml
framework_name: playground
http_ip: 0.0.0.0
http_port: 8080

log:
  glog:
    color_log: true
    also_log_to_stderr: true
    max_log_size: 1024
    stop_logging_if_full_disk: true
    log_dir: log

resource_mgr:
  update_interval: 5

lifecycle_mgr:
  clear_stale_heartbeats_interval: 10
```

## 字段说明

| 字段 | 说明 |
|------|------|
| `framework_name` | 框架实例名称，用于日志和资源标识 |
| `http_ip` | HTTP 服务监听地址 |
| `http_port` | HTTP 服务监听端口 |
| `log.glog` | glog 日志配置，参考 [日志配置](/guide/release-highlights/log) |
| `log.glog.color_log` | 终端彩色日志输出 |
| `log.glog.also_log_to_stderr` | 同时输出到 stderr |
| `log.glog.max_log_size` | 单个日志文件最大大小（KB） |
| `log.glog.stop_logging_if_full_disk` | 磁盘满时停止写日志 |
| `log.glog.log_dir` | 日志目录（相对于工作目录） |
| `resource_mgr.update_interval` | 资源管理器扫描间隔（秒） |
| `lifecycle_mgr.clear_stale_heartbeats_interval` | 清理过期心跳间隔（秒） |

> 完整配置项（包括 `insightos-log`、`discovery_mgr`、`source_urls` 等）见 [配置文件参考](/guide/configuration/config-file)。
