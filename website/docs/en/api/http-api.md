# HTTP API

> Framework version: **v2.6.0+** &nbsp;|&nbsp; Default listen address: `0.0.0.0:8080` &nbsp;|&nbsp; Content-Type: `application/json`

AbilityFramework is the ability runtime on a robot node, exposed to the upper layer (Studio / MCP server / integrators) via an HTTP REST API. This document enumerates all public endpoints in the current version, grouped by purpose, with usage examples.

## Common conventions

| Item | Convention |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Base URL | `http://<host>:8080` |
| Auth | The current version has **no authentication** (LAN / same-node trust). JWT is planned for a later version. |
| Encoding | Both requests and responses are UTF-8; Content-Type `application/json` (a few are `text/plain` / `text/markdown`) |
| Time format | RFC 3339 UTC (`2026-04-22T10:30:00Z`) |
| Errors | 4xx / 5xx use [RFC 7807 Problem Detail](https://datatracker.ietf.org/doc/html/rfc7807) JSON: `{"type":"...","title":"...","status":404,"detail":"..."}` |
| Idempotency | DELETE is usually idempotent (deleting something nonexistent does not error); POST `/api/instance` is constrained by `singleton`; POST `/api/lifecycle-request` with the same command returns the same taskId |
| Async tasks | Several POST endpoints return `{"taskId":"..."}`; the caller polls `GET /api/task/:id` to track the result (recommended 200ms interval, 30s upper bound) |
| Heartbeat cleanup | An `ability-heartbeat` not updated within 1 minute is considered offline by the framework and cleaned up |

---

## 1. Health check and configuration

Service liveness, configuration queries, and log retrieval. After the Studio launcher / mcp-server starts, it periodically polls this group to confirm the framework is available.

### GET /api/hello

**Scenario**: a liveness probe; the cheapest "is the framework online" check.

**Response 200**:

```
this is ability framework
```

```bash
curl http://localhost:8080/api/hello
```

**Spec**: no body; returns `text/plain`; any 2xx means reachable; < 10ms.

### GET /api/config

**Scenario**: read the framework runtime configuration (port, framework_name, submodule switches, etc.).

**Response 200**:

```json
{
  "http_port": 8080,
  "framework_name": "robot-lab-01",
  "discovery": { "multicast_group": "239.0.0.1", "port": 30001 },
  "...": "..."
}
```

```bash
curl http://localhost:8080/api/config
```

**Spec**: content comes from `config.yaml`, auto-converted from YAML to JSON; returns 404 if the config file is missing.

### GET /api/log

**Scenario**: pull the most recent runtime logs for on-site debugging (no need to ssh into the node to cat the log).

**Query**: `lines` (int, default 100, max 5000), `level` (`INFO`/`WARNING`/`ERROR`, default `INFO`; only returns lines at or above this level).

**Response 200**:

```json
["2026-04-22T03:00:00Z [I] ResourceManager::update()", "..."]
```

```bash
curl 'http://localhost:8080/api/log?lines=50&level=WARNING'
```

**Spec**: returns `[]` instead of 404 when the log file does not exist.

---

## 2. Ability manifest (Manifest) and CRD metadata

A Manifest is the ability description written by the package author (task list + parameter schema + configuration schema); a CRD is the framework-built-in resource type spec (used to constrain the validity of manifest / CR YAML).

### GET /api/manifest

**Scenario**: list the manifest summaries of all abilities loaded by the framework. Studio uses it to render cards on the "ability store" page.

**Response 200**:

```json
[
  {
    "abilityName": "EndpointControl.Robotics",
    "kind": "AtomAbility",
    "package": "robotics.endpoint.control",
    "version": "1.0.0",
    "taskCount": 5,
    "hasConfigSchema": true
  }
]
```

```bash
curl http://localhost:8080/api/manifest | jq
```

**Spec**: metadata only; to get the full schema use `GET /api/manifest/:name/:version`.

### GET /api/manifest/:name/:version

**Scenario**: get the full manifest of a single ability, including the input/output schema of each task and the configuration schema. The Studio form editor consumes this schema.

**Response 200** (excerpt):

```json
{
  "abilityName": "EndpointControl.Robotics",
  "version": "1.0.0",
  "tasks": [
    {
      "name": "MoveToPose",
      "parameters": { "type": "object", "properties": { "x": { "type": "number" } } },
      "returns":    { "type": "object" }
    }
  ],
  "configSchema": { "type": "object", "properties": { "robotUri": { "type": "string" } } }
}
```

```bash
curl http://localhost:8080/api/manifest/EndpointControl.Robotics/1.0.0
```

**Spec**: returns 404 if the version does not exist.

### GET /api/crd

**Scenario**: list all CRDs (custom resource types). Can be filtered.

**Query**: `abilityName` / `package` / `kind` (when `kind=device`, only device CRDs are returned).

**Response 200**: a CRD array.

```bash
curl 'http://localhost:8080/api/crd?package=robotics.endpoint.control'
```

**Spec**: all query parameters are combined with AND.

### GET /api/crd/:name/:version

**Scenario**: get a single CRD by name + version (including the full openAPIV3Schema definition).

```bash
curl http://localhost:8080/api/crd/AtomAbility/v1
```

**Spec**: returns 400 invalid argument if missing.

### GET /api/builtin-crd/ability

**Scenario**: get the framework's built-in AtomAbility CRD JSON Schema. Studio uses it to validate the manifest locally (catching errors early, saving an upload round-trip).

```bash
curl http://localhost:8080/api/builtin-crd/ability > ability.crd.schema.json
```

**Spec**: returns a JSON Schema (Draft-07); idempotent.

### GET /api/builtin-crd/service

**Scenario**: same as above, but returns the Service CRD schema.

```bash
curl http://localhost:8080/api/builtin-crd/service > service.crd.schema.json
```

---

## 3. CR templates (read-only)

A CR (Custom Resource) is the static template of an ability, determined by the `crs/*.yaml` embedded in the ability package or by the `crs/*.yaml` in the working directory. At runtime you **cannot create / modify / delete** it — you can only derive an instance from a CR.

### GET /api/cr

**Scenario**: list all CR templates (ability CRs, or device CRs specified by Query). The data source for the Studio "instantiate" dropdown.

**Query**: `abilityName` / `version` / `package` / `nodeId` / `nodeName` / `nodeAddr` / `kind=device`.

**Response 200**: a CR array.

```bash
curl 'http://localhost:8080/api/cr?abilityName=EndpointControl.Robotics'
```

### GET /api/cr/:id

**Scenario**: get a single CR by UUID. If the given id is an instance_id, the framework looks up the original CR from the instance's spec_snapshot. The SDK commonly uses this to self-identify "which CR am I".

```bash
curl http://localhost:8080/api/cr/4f9e2c1a-...
```

**Spec**: returns 404 if not found.

### GET /api/device_crs

**Scenario**: list only device CRs (to avoid mixing them with ability CRs).

```bash
curl http://localhost:8080/api/device_crs
```

### GET /api/crs

**Scenario**: list all YAML files under the `crs/` directory (without parsing). Used by the Studio "workspace file manager".

**Response 200**:

```json
{ "files": ["endpoint-control.cr.yaml", "nav.cr.yaml"], "total": 2 }
```

```bash
curl http://localhost:8080/api/crs
```

### POST /api/crs/autostart

**Scenario**: atomically modify the `spec.autoStart` field of a CR YAML file (turn on/off auto-launch at startup). Safer than having users hand-edit YAML.

**Request**:

```json
{ "filename": "endpoint-control.cr.yaml", "autoStart": true }
```

**Response 200**: `{"success":true,"filename":"endpoint-control.cr.yaml","autoStart":true}`

```bash
curl -X POST http://localhost:8080/api/crs/autostart \
  -H 'Content-Type: application/json' \
  -d '{"filename":"endpoint-control.cr.yaml","autoStart":true}'
```

**Spec**: returns 500 if the file does not exist or the write fails; does not modify any other field in the YAML.

### POST /api/cr  &nbsp;<sup style="color:#f85149">410 Gone</sup>

Deprecated. CRs are determined by packages/YAML and are no longer created at runtime. To add a CR, place the YAML in `crs/` and the framework's reconcile will load it automatically.

### DELETE /api/cr/:id  &nbsp;<sup style="color:#f85149">410 Gone</sup>

Deprecated. Same as above — delete the YAML and wait for reconcile.

---

## 4. Ability instance lifecycle

An Instance is the runtime copy of a CR template. The framework derives instances from CRs on demand, starts subprocesses, monitors them, and destroys them.

### GET /api/instance

**Scenario**: list all running or terminated instances.

**Response 200**:

```json
[
  {
    "instance_id": "uuid",
    "cr_id": "template-uuid",
    "cr_name": "endpoint-control-1",
    "instance_name": "endpoint-control-1-run-1",
    "ability_name": "EndpointControl.Robotics",
    "ability_version": "1.0.0",
    "state": "Running",
    "start_time": "2026-04-22T10:30:00Z",
    "stop_time": null
  }
]
```

```bash
curl http://localhost:8080/api/instance
```

### GET /api/instance/:id

**Scenario**: details of a single instance, including the `spec_snapshot` (the CR snapshot at instantiation, used for auditing / tracing).

```bash
curl http://localhost:8080/api/instance/4f9e2c1a-...
```

### POST /api/instance

**Scenario**: derive and start an instance from a CR template. This is the **most common "start an ability" endpoint** — internally it chains the entire flow: download-package (if needed) → create → start → wait Standby → connect.

**Request**:

```json
{
  "template": "endpoint-control-1",
  "start": true,
  "connect": true
}
```

`template` can be a CR name (e.g. `endpoint-control-1`) or a CR UUID. `start` / `connect` default to true.

**Response 200**: `{"taskId":"task-uuid","template":"endpoint-control-1"}`

```bash
curl -X POST http://localhost:8080/api/instance \
  -H 'Content-Type: application/json' \
  -d '{"template":"endpoint-control-1"}'
# → poll: curl http://localhost:8080/api/task/<taskId>
```

**Spec**:

- If the CR has `spec.singleton=true` and a running instance already exists → **409 Conflict**.
- The taskId tracks the entire startup pipeline; when the final state is `finished`, the result contains the `instance_id`.

### DELETE /api/instance/:id

**Scenario**: terminate and destroy an instance. If the instance is Active, the framework first sends a lifecycle terminate and then deletes the row.

**Response 200**: `{"status":"ok"}`

```bash
curl -X DELETE http://localhost:8080/api/instance/4f9e2c1a-...
```

**Spec**: idempotent (a nonexistent id returns 404, but repeatedly deleting an already-deleted id does not error).

---

## 5. Fine-grained lifecycle control

`POST /api/instance` and `DELETE /api/instance/:id` are coarse-grained wrappers; this group of endpoints is for fine-grained scenarios (create without start, disconnect while keeping the instance for later reconnect, etc.).

### POST /api/lifecycle-request

**Scenario**: manually drive instance state transitions. State machine: `Created → Standby ⇄ Active(Running) → Terminated`.

**Request**:

```json
{
  "abilityInstanceId": "instance-uuid",
  "command": "start"
}
```

`command`: `start` | `connect` | `disconnect` | `terminate`.

**Response 200**: `{"taskId":"task-uuid"}`

```bash
curl -X POST http://localhost:8080/api/lifecycle-request \
  -H 'Content-Type: application/json' \
  -d '{"abilityInstanceId":"4f9e2c1a-...","command":"connect"}'
```

**Spec**: after returning a taskId it asynchronously waits to reach the target state, with a 15s timeout. Returns 404 if the instance does not exist.

### POST /api/ability-heartbeat

**Scenario**: **used internally by the SDK**; the ability subprocess periodically (default 5s) reports to the framework that it is alive. Regular clients should not call this.

**Request** (JSON or CBOR are both accepted):

```json
{
  "id": "instance-uuid",
  "abilityName": "EndpointControl.Robotics",
  "abilityVersion": "1.0.0",
  "state": "Running",
  "abilityPort": 9002,
  "IPCPort": 9001,
  "IPCProtocol": "http",
  "position": "localhost"
}
```

**Response 200**: `OK` (text/plain)

**Response 410 Gone**: the framework no longer recognizes this id (the instance has been explicitly destroyed); the SDK should self-destruct and exit.

**Spec**: heartbeats not refreshed for >1 minute are cleaned up automatically.

### GET /api/ability-heartbeat

**Scenario**: for debugging — see which instances are actually reporting right now.

```bash
curl http://localhost:8080/api/ability-heartbeat
```

### GET /api/ability-heartbeat/:id

**Scenario**: the most recent heartbeat of a single instance.

```bash
curl http://localhost:8080/api/ability-heartbeat/4f9e2c1a-...
```

**Spec**: returns 404 for unregistered instances.

---

## 6. Ability proxy (task invocation)

Transparently forwards `/api/ability/:id/<rest>` to the `abilityPort` the ability process registered in its heartbeat. The client only talks to the framework and does not need to know each instance's port.

### `{GET,POST,PUT,DELETE} /api/ability/:id/:subpath`

**Scenario**: call the business API provided by an ability instance. The most common is `POST /api/ability/<id>/api/task/start` to start a task.

**Request**: passed through; body / headers / method are all forwarded.

**Response**: the proxied service's response is passed through.

```bash
# start a MoveToPose task
curl -X POST http://localhost:8080/api/ability/4f9e2c1a-.../api/task/start \
  -H 'Content-Type: application/json' \
  -d '{"task_type":"MoveToPose","params":{"x":0.5,"y":0.2,"z":0.3}}'

# list all tasks on the instance
curl http://localhost:8080/api/ability/4f9e2c1a-.../api/task/list
```

**Spec**: returns 404 when the instance does not exist or is not Running; returns 502 on network-layer errors (the ability process crashed).

---

## 7. Framework internal tasks

Asynchronous task tracking (starting an instance, downloading a package, etc., are all wrapped as tasks).

### GET /api/task

**Scenario**: list all task UUIDs. `?active=true` lists only those not yet finished.

```bash
curl 'http://localhost:8080/api/task?active=true'
```

### GET /api/task/:id

**Scenario**: query task status — use it to poll after async endpoints like `POST /api/instance` return a taskId.

**Response 200**:

```json
{
  "id": "task-uuid",
  "name": "auto-start-ability",
  "state": "finished",
  "error": null,
  "result": { "instance_id": "..." }
}
```

`state`: `running` / `finished` / `error` / `cancelled`.

```bash
curl http://localhost:8080/api/task/task-uuid
```

**Spec**: returns error for unknown ids.

### POST /api/task

**Scenario**: submit a framework-level custom task (requires that the corresponding task_type factory is registered).

**Request**:

```json
{ "task_type": "test.hello", "payload": { "a": 1 } }
```

**Response 200**: `{"taskId":"..."}`

```bash
curl -X POST http://localhost:8080/api/task \
  -H 'Content-Type: application/json' \
  -d '{"task_type":"test.hello","payload":{}}'
```

---

## 8. Ability package management

Publish / unpublish ability packages (zip format, containing `package.yaml` + `ability.manifest.yaml` + binaries).

### POST /api/package

**Scenario**: upload a new ability package (first publish) or update an existing package.

**Request**: `Content-Type: application/zip` (raw body) or `multipart/form-data` with field name `file`.

**Query**: `force=true` to force-overwrite an existing package with the same name+version.

**Response 200**:

```json
{
  "package": "robotics.endpoint.control",
  "version": "1.0.0",
  "crCount": 1,
  "status": "ok"
}
```

```bash
# raw zip
curl -X POST http://localhost:8080/api/package \
  -H 'Content-Type: application/zip' \
  --data-binary @robotics.endpoint.control.zip

# force overwrite
curl -X POST 'http://localhost:8080/api/package?force=true' \
  -H 'Content-Type: application/zip' \
  --data-binary @robotics.endpoint.control.zip
```

**Spec**: auto-reconciles after upload, mirroring the package's CRs into the database; size limit 100MB (413 if exceeded).

### GET /api/package

**Scenario**: list uploaded packages (the current implementation returns `[]`; planned).

### DELETE /api/package/:name/:version

**Scenario**: unpublish a package; after the framework reconciles, the package's CRs are unmirrored; running instances are unaffected.

```bash
curl -X DELETE http://localhost:8080/api/package/robotics.endpoint.control/1.0.0
```

---

## 9. Skill documents

Ability packages can carry `*.md` files to act as behavioral guides for LLM agents ("when calling these tasks, do X"). The MCP server exposes these skills as `skill://` resources to Claude/OpenAI agents.

### GET /api/skill

**Scenario**: list all skill metadata (path, size, first-line title).

**Response 200**:

```json
[
  {
    "package": "robotics.endpoint.control",
    "version": "1.0.0",
    "filename": "tasks/move-to-pose.md",
    "size_bytes": 1234,
    "title": "MoveToPose"
  }
]
```

```bash
curl http://localhost:8080/api/skill
```

### GET /api/skill/:package/:version/:filename

**Scenario**: get the raw markdown of a single skill. `:filename` may include subdirectories (e.g. `tasks/move-to-pose.md`).

```bash
curl 'http://localhost:8080/api/skill/robotics.endpoint.control/1.0.0/tasks/move-to-pose.md'
```

**Response 200**: `text/markdown; charset=utf-8`; the body is raw markdown.

---

## 10. Resource occupation

Control whether instances share or exclusively occupy a CR (typical scenario: one robotic-arm CR shared by two abilities vs. exclusively occupied).

### GET /api/cr/:id/occupation

**Scenario**: check who is occupying a resource.

**Response 200**:

```json
{
  "owner": { "abilityInstanceId": "uuid", "position": "localhost" },
  "sharers": { "instance-uuid-1": "pos1", "instance-uuid-2": "pos2" }
}
```

```bash
curl http://localhost:8080/api/cr/4f9e2c1a-.../occupation
```

### POST /api/cr/:id/occupation

**Scenario**: request to occupy a resource (shared / exclusive). Called by the SDK or the orchestration layer before the ability starts.

**Request**:

```json
{ "occupy_id": "instance-uuid", "mode": "shared", "position": "localhost" }
```

`mode`: `shared` or `unique`.

**Response 200**: `OK` (text/plain)

**Response 409 Conflict**: a `unique` request encountered an already-occupied resource.

```bash
curl -X POST http://localhost:8080/api/cr/4f9e2c1a-.../occupation \
  -H 'Content-Type: application/json' \
  -d '{"occupy_id":"instance-uuid","mode":"shared","position":"localhost"}'
```

### DELETE /api/cr/:id/occupation

**Scenario**: release an occupation.

**Request**:

```json
{ "occupy_id": "instance-uuid", "mode": "shared" }
```

```bash
curl -X DELETE http://localhost:8080/api/cr/4f9e2c1a-.../occupation \
  -H 'Content-Type: application/json' \
  -d '{"occupy_id":"instance-uuid","mode":"shared"}'
```

---

## 11. Discovery and teams

Cross-node discovery, teaming, election, and broadcast heartbeats. Used in multi-robot collaboration scenarios.

### GET /api/discovery

**Scenario**: the framework's own IPv4 list (used in multi-NIC environments to pick a reachable address).

**Response 200**: `{"ipv4":["127.0.0.1","192.168.1.5"]}`

```bash
curl http://localhost:8080/api/discovery
```

### POST /api/discovery

**Scenario**: query another framework's IPv4 by framework_id.

**Request**: `{"id":"framework-uuid"}`

**Response 200**: `{"ipv4":["..."]}`

### GET /api/team

**Scenario**: list all teams the current framework has joined.

```bash
curl http://localhost:8080/api/team
```

### POST /api/team/join

**Scenario**: join a team (requires a valid JWT).

**Request**: `{"teamID":"team-uuid","jwt":"<token>"}`

**Response 200**: `Successfully joined team` or `Already in team` (text/plain).

```bash
curl -X POST http://localhost:8080/api/team/join \
  -H 'Content-Type: application/json' \
  -d '{"teamID":"team-uuid","jwt":"..."}'
```

**Spec**: returns error on JWT validation failure; idempotent (repeated join returns already).

### POST /api/team/leave

**Scenario**: leave a specified team.

**Request**: `{"teamID":"team-uuid"}`

**Response 200**: `Successfully left team`

```bash
curl -X POST http://localhost:8080/api/team/leave \
  -H 'Content-Type: application/json' \
  -d '{"teamID":"team-uuid"}'
```

### GET /api/team/masters

**Scenario**: list the known master frameworks (the election result).

### GET /api/team/peers

**Scenario**: list the runtime information of other nodes in the same team (IP / port / last_updated).

### POST /api/team-heartbeat

**Scenario**: **used internally between frameworks**; receives heartbeats from other team members. Regular clients do not call this.

### POST /api/team/election-msg

**Scenario**: **used internally between frameworks**; election message reception. Schema to be added.

### POST /api/findAbility

**Scenario**: search the entire team for an available instance of an ability (for cross-node orchestration). Schema to be added.

---

## 12. Controller heartbeat

### POST /api/controller-heartbeat

**Scenario**: **used internally by the Controller subprocess**; periodically pings the framework. Regular clients do not call this.

**Request** (JSON or CBOR):

```json
{ "controllerInstanceId": "controller-...", "...": "..." }
```

**Response 200**: no body.

---

## 13. Service CR (experimental)

CRUD for Service-type CRs (distinguished from ability CRs; ability CRs are read-only, while Service CRs still allow runtime add/delete).

### GET /api/service-cr

List all service CRs.

### GET /api/service-cr/:id

Query a single service CR by id; returns 404 if it does not exist.

### POST /api/service-cr

**Scenario**: create a service CR.

**Request**: full Service CR JSON (the framework generates the id if not provided).

**Response 201**: `{"id":"uuid","status":"created"}`

```bash
curl -X POST http://localhost:8080/api/service-cr \
  -H 'Content-Type: application/json' \
  -d @service.cr.json
```

### DELETE /api/service-cr/:id

Delete a service CR. Response 200: `{"status":"deleted"}`.

---

## 14. Internal test API

Used only for framework self-checks / dev-box debugging; **should not be called in production**, and may be removed or access-controlled in future versions.

### POST /api/internal/test-message

**Scenario**: inject a test message into the internal message bus (synchronously wait for a response or fire-and-forget asynchronously).

**Request**:

```json
{
  "source": "TestClient",
  "destination": "TargetModule",
  "operation": "test_op",
  "payload": "{}",
  "synchronous": true
}
```

**Response 200**: returns the target's response if synchronous; otherwise `OK`.

### POST /api/internal/test-download-package

**Scenario**: trigger a package download flow (without actually deploying) to verify the mirror configuration.

**Request**: `{"package":"...","version":"..."}`

**Response 200**: `OK` (text/plain)

---

## Endpoint summary

| Method              | Path                                     | Group        | One-liner                          |
| ------------------- | ---------------------------------------- | ------------ | ---------------------------------- |
| GET                 | `/api/hello`                             | Health       | Liveness probe                     |
| GET                 | `/api/config`                            | Health       | Get global configuration           |
| GET                 | `/api/log`                               | Health       | Get the last N log lines           |
| GET                 | `/api/manifest`                          | Manifest/CRD | List ability manifest summaries    |
| GET                 | `/api/manifest/:name/:version`           | Manifest/CRD | Full manifest of a single ability  |
| GET                 | `/api/crd`                               | Manifest/CRD | List CRDs (filterable)             |
| GET                 | `/api/crd/:name/:version`                | Manifest/CRD | A single CRD                       |
| GET                 | `/api/builtin-crd/ability`               | Manifest/CRD | Built-in AtomAbility CRD schema    |
| GET                 | `/api/builtin-crd/service`               | Manifest/CRD | Built-in Service CRD schema        |
| GET                 | `/api/cr`                                | CR templates | List CR templates                  |
| GET                 | `/api/cr/:id`                            | CR templates | A single CR (supports instance_id fallback) |
| GET                 | `/api/device_crs`                        | CR templates | List device CRs                    |
| GET                 | `/api/crs`                               | CR templates | List YAML files                    |
| POST                | `/api/crs/autostart`                     | CR templates | Modify the autoStart field         |
| GET                 | `/api/instance`                          | Instance     | List all instances                 |
| GET                 | `/api/instance/:id`                      | Instance     | Details of a single instance       |
| POST                | `/api/instance`                          | Instance     | Derive from CR and start           |
| DELETE              | `/api/instance/:id`                      | Instance     | Terminate and destroy              |
| POST                | `/api/lifecycle-request`                 | Lifecycle    | Drive a state transition           |
| POST                | `/api/ability-heartbeat`                 | Lifecycle    | SDK reports heartbeat              |
| GET                 | `/api/ability-heartbeat`                 | Lifecycle    | List all heartbeats                |
| GET                 | `/api/ability-heartbeat/:id`             | Lifecycle    | Heartbeat of a single instance     |
| GET/POST/PUT/DELETE | `/api/ability/:id/:subpath`              | Ability proxy | Transparently forward to abilityPort |
| GET                 | `/api/task`                              | Task         | List all tasks                     |
| GET                 | `/api/task/:id`                          | Task         | Query task status                  |
| POST                | `/api/task`                              | Task         | Submit a custom task               |
| POST                | `/api/package`                           | Package      | Publish a package                  |
| GET                 | `/api/package`                           | Package      | List packages (planned)            |
| DELETE              | `/api/package/:name/:version`            | Package      | Unpublish a package                |
| GET                 | `/api/skill`                             | Skill        | List skill metadata                |
| GET                 | `/api/skill/:package/:version/:filename` | Skill        | Raw content of a single skill      |
| GET                 | `/api/cr/:id/occupation`                 | Occupation   | Query occupation                   |
| POST                | `/api/cr/:id/occupation`                 | Occupation   | Request occupation                 |
| DELETE              | `/api/cr/:id/occupation`                 | Occupation   | Release occupation                 |
| GET                 | `/api/discovery`                         | Discovery    | This host's IPv4                   |
| POST                | `/api/discovery`                         | Discovery    | Query a remote IPv4 by ID          |
| GET                 | `/api/team`                              | Team         | Teams joined                       |
| POST                | `/api/team/join`                         | Team         | Join                               |
| POST                | `/api/team/leave`                        | Team         | Leave                              |
| GET                 | `/api/team/masters`                      | Team         | Known masters                      |
| GET                 | `/api/team/peers`                        | Team         | Same-team members                  |
| POST                | `/api/team-heartbeat`                    | Team         | Inter-framework heartbeat          |
| POST                | `/api/team/election-msg`                 | Team         | Election message                   |
| POST                | `/api/findAbility`                       | Team         | Cross-node ability search          |
| POST                | `/api/controller-heartbeat`              | Controller   | controller heartbeat               |
| GET                 | `/api/service-cr`                        | Service CR   | List                               |
| GET                 | `/api/service-cr/:id`                    | Service CR   | Single                             |
| POST                | `/api/service-cr`                        | Service CR   | Create                             |
| DELETE              | `/api/service-cr/:id`                    | Service CR   | Delete                             |
| POST                | `/api/internal/test-message`             | Internal test | Test the message bus               |
| POST                | `/api/internal/test-download-package`    | Internal test | Test package download              |

A total of 47 public endpoints (excluding the deprecated `POST/DELETE /api/cr` and the legacy `/api/resourcemgr/*`).
