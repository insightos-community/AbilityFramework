# REST API

The framework manages ability instances via the HTTP REST API. This document describes the typical lifecycle flow: uploading a package, creating and starting an instance, checking status, stopping, and deleting.

## 1. Upload an Ability Package

```sh
curl -X POST http://localhost:8080/api/package \
  -F "file=@my_ability.zip;type=application/zip"
```

The framework auto-extracts to `packages/<package_name>/<version>/`. You can also upload via the WebUI debug page.

## 2. Create and Start an Ability Instance

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

- `start=true` — auto-start after creation
- `connect=true` — auto-connect after startup

The response includes a `taskId` for tracking the async task. If the package is not present locally, the framework downloads it from a configured `source_urls` address.

## 3. Check the Running Status

```sh
# heartbeats for all ability instances
curl http://localhost:8080/api/ability-heartbeat

# a specific instance
curl http://localhost:8080/api/ability-heartbeat/<instance-id>
```

## 4. Stop an Ability Instance

```sh
curl -X POST http://localhost:8080/api/lifecycle-request \
  -H "Content-Type: application/json" \
  -d '{
    "abilityInstanceId": "<instance-id>",
    "command": "terminate"
  }'
```

## 5. Delete an Ability Instance

```sh
curl -X DELETE "http://localhost:8080/api/cr/<instance-id>?force=true"
```
