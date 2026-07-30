# Quick start

Start AbilityFramework, upload an ability package, instantiate it, and invoke a task.

> If you don't have an ability package (`.zip`) yet, read [First project](./first-project) first to build and package an ability.

## Build the framework working directory

```bash
mkdir -p framework
cp AbilityFramework framework/
chmod +x framework/AbilityFramework

# Minimal config.yaml
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

> For the full `config.yaml` field reference, see [Configuration reference](./configuration), or the [Configuration file reference](/en/guide/configuration/config-file).

## Install the SDK into the system Python

The framework spawns ability subprocesses using the system `python3` (not the venv), so make sure the SDK is installed:

```bash
pip3 install --user ability_py-0.4.0-py3-none-any.whl
# Or, if you use the deb package: sudo dpkg -i python3-ability-py_0.4.0_all.deb
```

## Start the framework

```bash
cd framework
./AbilityFramework &
cd ..
```

Wait for the output:

```
  AbilityFramework is running

  API:   http://localhost:8080/api/hello
  WebUI: http://localhost:8080/ui
```

Verify:

```bash
curl -s http://localhost:8080/api/hello
# → this is ability framework
```

## Upload an ability package

```bash
curl -X POST http://localhost:8080/api/package \
    -H 'Content-Type: application/zip' \
    --data-binary @mock.arm.demo-1.0.0.zip
```

## Verify the ability is loaded

```bash
# View CR templates
curl -s http://localhost:8080/api/cr | python3 -m json.tool
# You should see the mockarm-demo-1 template

# View manifests
curl -s http://localhost:8080/api/manifest | python3 -m json.tool
# You should see the MockArm.Demo ability with taskCount=2

# View skills
curl -s http://localhost:8080/api/skill | python3 -m json.tool
# You should see a mock-arm-control skill entry
```

## Start an instance

**Option A — curl**:

```bash
# Start an instance
RESP=$(curl -s -X POST http://localhost:8080/api/instance \
    -H 'Content-Type: application/json' \
    -d '{"template":"mockarm-demo-1","start":true,"connect":true}')
echo "$RESP" | python3 -m json.tool
TASK_ID=$(echo "$RESP" | python3 -c 'import sys,json; print(json.load(sys.stdin)["taskId"])')

# Wait for Running
for i in $(seq 1 30); do
    STATE=$(curl -s http://localhost:8080/api/instance \
        | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0]["state"] if d else "none")' 2>/dev/null)
    echo "  state=$STATE"
    [ "$STATE" = "Running" ] && break
    sleep 1
done

# Get the instance ID
INST_ID=$(curl -s http://localhost:8080/api/instance \
    | python3 -c 'import sys,json; print(json.load(sys.stdin)[0]["instance_id"])')
echo "instance: $INST_ID"
```

**Option B — WebUI**:

Open `http://localhost:8080/ui` in a browser and use the "Running instances" panel.

## Invoke a task

```bash
# MoveTo
curl -s -X POST "http://localhost:8080/api/ability/$INST_ID/api/task/start" \
    -H 'Content-Type: application/json' \
    -d '{"task_type":0,"input":{"x":0.3,"y":0.1,"z":0.2,"speed":0.5}}' \
    | python3 -m json.tool

# Wait for the task to finish
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

## Use the debug console (optional)

```bash
# Start the openapi-tool debug console
./openapi-tool-v1.0.0-linux-x86_64 serve --port 5000
```

Open `http://localhost:5000` in a browser, enable the **"Framework mode"** toggle in the sidebar, and set the framework address to `http://localhost:8080`:

- The CR template list appears on the left; click ▶ to start an instance
- After selecting an instance, the task list shows on the right
- Fill in the parameters and click "Invoke"

## Stop the instance

```bash
curl -s -X DELETE "http://localhost:8080/api/instance/$INST_ID" \
    | python3 -m json.tool
# → {"status": "ok"}
```

## Uninstall the ability package (optional)

```bash
curl -X DELETE http://localhost:8080/api/package/mock.arm.demo/1.0.0
```
