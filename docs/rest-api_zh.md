# REST API

框架通过 HTTP REST API 管理能力实例。本文档描述典型的生命周期流程：上传能力包、创建并启动实例、查看状态、停止、删除。

## 1. 上传能力包

```sh
curl -X POST http://localhost:8080/api/package \
  -F "file=@my_ability.zip;type=application/zip"
```

上传后框架会自动解压到 `packages/<package_name>/<version>/` 目录。也可通过 WebUI 的框架调试页面上传。

## 2. 创建并启动能力实例

```sh
curl -X POST "http://localhost:8080/api/cr?start=true&connect=true" \
  -H "Content-Type: application/json" \
  -d '{
    "kind": "AtomAbility",
    "metadata": { "name": "my-instance" },
    "spec": {
      "package": "my.ability.org",
      "version": "1.0.0",
      "abilityName": "MyAbility.org",
      "position": "localhost"
    }
  }'
```

- `start=true` — 创建后自动启动
- `connect=true` — 启动后自动连接

返回值包含 `taskId`，可用于查询异步任务进度。如果本地不存在该能力包，框架会自动从 `source_urls` 配置的地址下载。

## 3. 查看运行状态

```sh
# 查看所有能力实例的心跳
curl http://localhost:8080/api/ability-heartbeat

# 查看指定实例
curl http://localhost:8080/api/ability-heartbeat/<instance-id>
```

## 4. 停止能力实例

```sh
curl -X POST http://localhost:8080/api/lifecycle-request \
  -H "Content-Type: application/json" \
  -d '{
    "abilityInstanceId": "<instance-id>",
    "command": "terminate"
  }'
```

## 5. 删除能力实例

```sh
curl -X DELETE "http://localhost:8080/api/cr/<instance-id>?force=true"
```
