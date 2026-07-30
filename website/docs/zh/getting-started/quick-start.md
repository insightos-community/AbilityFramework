# 快速开始

启动 AbilityFramework、上传能力包、实例化并调用任务。

> 如果你还没有能力包（`.zip`），请先阅读[第一个项目](./first-project)完成能力开发与打包。

## 构建框架工作目录

```bash
mkdir -p framework
cp AbilityFramework framework/
chmod +x framework/AbilityFramework

# 最小 config.yaml
cat > framework/config.yaml << 'EOF'
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
EOF
```

> 完整 `config.yaml` 字段说明见 [配置参考](./configuration)，或查阅 [配置文件参考](/guide/configuration/config-file)。

## 安装 SDK 到系统 Python

框架启动能力子进程时用的是系统 `python3`（不是 venv），需要确保 SDK 已安装：

```bash
pip3 install --user ability_py-0.4.0-py3-none-any.whl
# 或者如果用 deb: sudo dpkg -i python3-ability-py_0.4.0_all.deb
```

## 启动框架

```bash
cd framework
./AbilityFramework &
cd ..
```

等待输出：

```
  AbilityFramework is running

  API:   http://localhost:8080/api/hello
  WebUI: http://localhost:8080/ui
```

验证：

```bash
curl -s http://localhost:8080/api/hello
# → this is ability framework
```

## 上传能力包

```bash
curl -X POST http://localhost:8080/api/package \
    -H 'Content-Type: application/zip' \
    --data-binary @mock.arm.demo-1.0.0.zip
```

## 验证能力已加载

```bash
# 查看 CR 模板
curl -s http://localhost:8080/api/cr | python3 -m json.tool
# 应该看到 mockarm-demo-1 模板

# 查看 manifest
curl -s http://localhost:8080/api/manifest | python3 -m json.tool
# 应该看到 MockArm.Demo 能力，taskCount=2

# 查看 skill
curl -s http://localhost:8080/api/skill | python3 -m json.tool
# 应该看到 mock-arm-control skill 条目
```

## 启动实例

**方式 A — curl**：

```bash
# 启动实例
RESP=$(curl -s -X POST http://localhost:8080/api/instance \
    -H 'Content-Type: application/json' \
    -d '{"template":"mockarm-demo-1","start":true,"connect":true}')
echo "$RESP" | python3 -m json.tool
TASK_ID=$(echo "$RESP" | python3 -c 'import sys,json; print(json.load(sys.stdin)["taskId"])')

# 等待 Running
for i in $(seq 1 30); do
    STATE=$(curl -s http://localhost:8080/api/instance \
        | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0]["state"] if d else "none")' 2>/dev/null)
    echo "  state=$STATE"
    [ "$STATE" = "Running" ] && break
    sleep 1
done

# 获取实例 ID
INST_ID=$(curl -s http://localhost:8080/api/instance \
    | python3 -c 'import sys,json; print(json.load(sys.stdin)[0]["instance_id"])')
echo "instance: $INST_ID"
```

**方式 B — WebUI**：

浏览器打开 `http://localhost:8080/ui`，在"运行实例"面板中操作。

## 调用任务

```bash
# MoveTo
curl -s -X POST "http://localhost:8080/api/ability/$INST_ID/api/task/start" \
    -H 'Content-Type: application/json' \
    -d '{"task_type":0,"input":{"x":0.3,"y":0.1,"z":0.2,"speed":0.5}}' \
    | python3 -m json.tool

# 等任务完成
TASK=$(curl -s -X POST "http://localhost:8080/api/ability/$INST_ID/api/task/status" \
    -H 'Content-Type: application/json' \
    -d "{\"task_id\":\"$(curl -s -X POST "http://localhost:8080/api/ability/$INST_ID/api/task/start" \
        -H 'Content-Type: application/json' \
        -d '{"task_type":0,"input":{"x":0.3,"y":0.1,"z":0.2,"speed":0.5}}' \
        | python3 -c 'import sys,json; print(json.load(sys.stdin)["task_id"])')\"}")

# GetPose
curl -s -X POST "http://localhost:8080/api/ability/$INST_ID/api/task/start" \
    -H 'Content-Type: application/json' \
    -d '{"task_type":1,"input":{}}' \
    | python3 -m json.tool
```

## 使用调试台（可选）

```bash
# 启动 openapi-tool 调试台
./openapi-tool-v1.0.0-linux-x86_64 serve --port 5000
```

浏览器打开 `http://localhost:5000`，打开侧栏的**"框架模式"**开关，框架地址填 `http://localhost:8080`：

- 左侧出现 CR 模板列表，点 ▶ 启动实例
- 选中实例后右侧显示任务列表
- 填入参数，点"发起调用"

## 停止实例

```bash
curl -s -X DELETE "http://localhost:8080/api/instance/$INST_ID" \
    | python3 -m json.tool
# → {"status": "ok"}
```

## 卸载能力包（可选）

```bash
curl -X DELETE http://localhost:8080/api/package/mock.arm.demo/1.0.0
```
