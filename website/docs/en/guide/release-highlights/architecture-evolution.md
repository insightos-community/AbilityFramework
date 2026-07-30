# Architecture evolution design

## Background

Currently, CRD files are carried by each ability package (`ability.crd.yaml` or `crds/*.crd.yaml`), mixing framework responsibilities with ability-developer responsibilities. Following the k8s CRD/CR pattern:

- A **CRD** should be a framework-built-in resource type definition (like a CRD provided by a k8s operator), defining the general structure of a resource
- A **CR** is the instance declaration written by the ability developer, describing a specific ability's spec, interface, and configuration

### Current problems

1. **Confused responsibilities** — the `AbilityCRD` struct mixes framework-level fields (ownership/bind modes, x-intentable, x-ref, FwkDetail) with ability-specific fields (provides, types, rpcMethods, tasks, config, debugOption, openAPIV3Schema)
2. **Redundant carriage** — every ability package carries a CRD, but the framework-level CRD schema is identical for all abilities
3. **Toolchain concept error** — openapi-tool's `generate-crd-from-openapi.py` names its output "CRD", but its actual purpose is to generate a CR file conforming to the ability spec from an OpenAPI definition; the tool confuses the concepts of CRD (framework spec) and CR (ability instance declaration)
4. **No Service type** — the framework only supports on-demand Abilities, not long-running Services

### Related projects

| Project | Role |
|------|------|
| **abilityframework** | The core framework, managing ability deployment, running, and monitoring |
| **openapi-tool** | Defines ability interfaces through OpenAPI YAML and generates CR files conforming to the CRD spec (`generate-crd-from-openapi.py`, currently misnamed as "generate CRD"), supporting extensions such as `x-insightos`/`x-ability-config`/`x-tasks`/`x-depends` |
| **ability-py-sdk** | Python ability-development SDK, providing the `AbilityInterface` base class (on_start/on_connect/on_disconnect/on_terminate), heartbeat management, and Flask IPC |
| **ability-sdk** | C++ ability-development SDK, providing AbilityStub, AbilityConfig, TaskHelper, etc. |

---

## Phase 1: CRD internalization, introducing the Manifest

### Core idea

Extract the CRD from ability packages into a framework-built-in definition. The ability-specific fields of the original CRD are migrated to a new file `ability.manifest.yaml` (the ability manifest).

### Responsibility division

```
Framework-maintained (CRD)              Ability-developer-written (Manifest + CR)
┌─────────────────────┐           ┌─────────────────────────┐
│ ability.crd          │           │ ability.manifest.yaml    │
│ - CR structure schema│           │ - provides              │
│ - kind enum          │           │ - types, rpcMethods     │
│ - spec common fields │           │ - tasks                 │
│ - metadata spec      │           │ - config/status schema  │
│ - x-intentable sem.  │           │ - depends               │
│ - x-ref semantics    │           │ - constants             │
└─────────────────────┘           └─────────────────────────┘
                                  ┌─────────────────────────┐
                                  │ my-ability.cr.yaml       │
                                  │ - kind: AtomAbility      │
                                  │ - metadata.name          │
                                  │ - spec.package/version   │
                                  │ - spec.config (values)   │
                                  │ - spec.devices           │
                                  └─────────────────────────┘
```

### CRD (framework built-in, single copy)

Embedded in the framework binary or loaded from `$ABILITY_FRAMEWORK_HOME/schema/ability.crd.yaml`:

```yaml
apiVersion: framework/v1
kind: CustomResourceDefinition
metadata:
  name: ability.crd
spec:
  names:
    kind: Ability           # covers AtomAbility, ComposeAbility, AbstractAbility
  schema:
    openAPIV3Schema:
      type: object
      required: [kind, metadata, spec]
      properties:
        kind:
          type: string
          enum: [AtomAbility, ComposeAbility, AbstractAbility]
        metadata:
          type: object
          required: [name]
          properties:
            name: { type: string }
            labels: { type: object, additionalProperties: { type: string } }
            annotations: { type: object, additionalProperties: { type: string } }
        spec:
          type: object
          required: [package, version, abilityName]
          properties:
            package: { type: string }
            version: { type: string }
            abilityName: { type: string }
            position: { type: string, default: "localhost" }
            autoStart: { type: boolean }
            priority: { type: integer }
            activityCondition: {}
            config: {}              # concrete schema defined by schema.config in the manifest
            debugOption: {}         # concrete schema defined by schema.debugOption in the manifest
            subabilities: { type: array }
            devices: { type: array }
            models: { type: array }
```

### Ability Manifest (ability manifest, carried by the package)

Written by the ability developer (can be generated via openapi-tool) and published with the package:

```yaml
abilityName: MyAbility.org
kind: AtomAbility
provides:                         # descriptions of the services the ability exposes
  - name: ObjectDetection
    description: "Detect objects in an image"
types: { ... }                    # custom type definitions
rpcMethods: { ... }               # RPC method signatures
tasks:                            # task definitions (corresponding to openapi-tool's x-tasks)
  - taskType: 0
    summary: "Run a single detection"
    params:
      - name: timeout
        type: int
        optional: true
constants: { ... }                # constants provided to the parent ability
schema:
  config:                         # OpenAPI V3 Schema validating CR spec.config
    openAPIV3Schema:
      type: object
      properties:
        threshold: { type: number, default: 0.5 }
  status:                         # Schema validating the runtime status
    openAPIV3Schema: { ... }
  debugOption:                    # Schema validating CR spec.debugOption
    openAPIV3Schema:
      type: object
      properties:
        logPicture: { type: boolean, default: false }
depends:                          # dependency declarations
  abilities:
    - abilityName: Sub.Ability
      ownership: unique
      bind: local
  devices:
    - deviceName: Camera
      ownership: shared
  models:                         # model dependencies (corresponding to openapi-tool's x-depends.models)
    - spec:
        name: yolov5
        architecture: object_detection
        framework: pytorch
```

### Package structure change

```
packages/<name>/<version>/
  package.yaml               # package manifest (name, version, arch)
  ability.manifest.yaml      # ability manifest (NEW, replaces the original CRD)
  bin/
    ability                  # executable
```

### Validation flow becomes two-level

1. **Framework-level validation** — the overall CR structure conforms to the built-in ability.crd schema
2. **Ability-level validation** — the CR's config/debugOption/status conform to the schemas in the manifest (x-intentable/x-ref expansion runs at this stage)

### openapi-tool adaptation

`generate-crd-from-openapi.py` needs to correct the concept and adjust the output:

- **Current (concept error)**: generates `*.crd.yaml` from OpenAPI YAML, but actually generates a CR rather than a CRD
- **After Phase 1 (concept corrected)**:
  - The script is renamed to `generate-cr-from-openapi.py`, clarifying that it generates a CR file
  - The output target changes to `*.cr.yaml` (a CR conforming to the ability.crd spec)
  - It also outputs `ability.manifest.yaml` (the ability manifest, containing interface descriptions, schemas, dependencies, etc.)
- The core conversion logic is unchanged (x-insightos → metadata, x-ability-config → spec.config, x-tasks → manifest.tasks, x-depends → manifest.depends); only the output format and field ownership are adjusted

### ability-py-sdk adaptation

The SDK itself needs no major changes. The current SDK initializes an ability by fetching CR information via `/api/cr/{uuid}`; this flow is unchanged. Impact points:

- The CR structure returned by the framework is unchanged (no CR fields changed)
- If the SDK needs to read the ability interface description (manifest content), a new `/api/manifest/{abilityName}/{version}` endpoint is added

### Key code changes

| File | Change |
|------|------|
| New `include/resourcemgr/ability_manifest.hpp` | AbilityManifest struct (extracted from AbilityCRD::Spec) |
| New `src/resourcemgr/ability_manifest.cpp` | JSON serialization, compatibility logic for converting from the old CRD |
| `include/resourcemgr/ability_crd.hpp` | Slimmed down, keeping only the framework-level schema definition |
| `include/resourcemgr/ability_pkg.hpp` | AbilityPackage gains a manifests field |
| `src/resourcemgr/resource_mgr.cpp` | read_package() now reads the manifest; the framework CRD is loaded once at startup |
| `src/resourcemgr/json_schema_utils.cpp` | Remove get_ability_crd_path(); validation becomes two-level (framework CRD + manifest) |
| `src/resourcemgr/resource_mgr_http_apis.cpp` | Adapt to the new validation path; add a manifest query endpoint |
| `openapi-tool/generate-crd-from-openapi.py` | Rename to `generate-cr-from-openapi.py`, output CR + manifest |

### Backward compatibility

- If the package contains a legacy `ability.crd.yaml` or `crds/*.crd.yaml`, the manifest fields are automatically extracted to build an in-memory AbilityManifest
- A deprecation warning log is emitted
- Legacy packages continue to work without changes

---

## Phase 1.5: Ability project scaffold tool

### Core idea

Combine openapi-tool and ability-py-sdk to provide a complete ability-development toolchain:

1. **openapi-tool** generates a CR file (ability declaration) from an OpenAPI definition
2. **The scaffold tool** reads the tasks definitions in the CR and, combined with SDK templates, automatically generates ability project code

Developers only need to: define the OpenAPI interface → generate the CR → generate the project → **implement on_start() initialization + each task's body**.

### Toolchain flow

```
  OpenAPI YAML                   CR YAML                     Ability project
  (interface def.)               (ability decl.)              (runnable code)

  x-tasks:                       spec:                       task.py:
    - taskType: 0        ──>       tasks:               ──>    class DetectTask(TaskInterface):
      summary: detect                - taskType: 0                def execute(self, input: dict) -> dict:
      params:                          params:                       timeout = input.get("timeout", 0)
        - name: timeout                  ...                          # TODO: implement detection
          type: int                                                   raise NotImplementedError
      returns:
        - name: result
          type: array

  ┌─────────────┐            ┌─────────────┐            ┌─────────────────────┐
  │ openapi-tool │    ──>    │ CR + Manifest│    ──>    │ scaffold-tool        │
  │ (define iface)│           │ (gen. decl.) │            │ (gen. project code) │
  └─────────────┘            └─────────────┘            └─────────────────────┘
```

### Scaffold-generated project structure

Taking a CR that defines 3 tasks (taskType: 0=Detect, 1=Track, 2=Classify) as an example:

```
my-ability/
  main.py                          # entry point, auto-generated, usually no need to change
  task.py                          # task implementation, developer fills in the body
  service/
    __init__.py                    # exports ImplAbility, task_manager
    ability.py                     # AbilityInterface implementation, developer fills in on_start
    server.py                      # Flask API server (copied from the SDK template)
    interface.py                   # TaskInterface base class (copied from the SDK template)
    task_manager.py                # TaskManager (copied from the SDK template)
  ability.cr.yaml                  # CR generated by openapi-tool
  ability.manifest.yaml            # Manifest generated by openapi-tool
```

### Generated task.py example

Assume the CR defines:

```yaml
spec:
  tasks:
    - taskType: 0
      summary: "Single recognition task"
      params:
        - name: timeout
          type: int
          optional: true
        - name: image_url
          type: string
      returns:
        - name: objects
          type: array
        - name: confidence
          type: number
```

The scaffold generates:

```python
from service import TaskInterface


class RecognizeTask(TaskInterface):
    """Single recognition task (taskType: 0)"""

    def execute(self, input_data: dict) -> dict:
        """
        Args:
            input_data: {
                "timeout": int (optional),
                "image_url": str,
            }

        Returns:
            {
                "objects": list,
                "confidence": float,
            }
        """
        timeout = input_data.get("timeout", 0)
        image_url = input_data.get("image_url", "")

        # TODO: implement recognition logic
        raise NotImplementedError("Please implement RecognizeTask.execute()")
```

### Generated ability.py example

```python
import ability_py
import threading
from .server import app
from werkzeug.serving import make_server


class ServerThread(threading.Thread):
    def __init__(self, host="0.0.0.0", port=8080):
        super().__init__()
        self.server = make_server(host, port, app)

    def run(self):
        self.server.serve_forever()

    def shutdown(self):
        self.server.shutdown()


class ImplAbility(ability_py.AbilityInterface):
    def __init__(self):
        self.ability_port = 0
        self.st = None

    def on_start(self):
        # TODO: initialize resources (e.g. load models, connect devices)
        pass

    def on_connect(self):
        if self.ability_port:
            return
        self.ability_port = ability_py.get_free_port()
        self.st = ServerThread(host="localhost", port=self.ability_port)
        self.st.start()

    def on_disconnect(self):
        if self.ability_port != 0:
            self.st.shutdown()
            self.ability_port = 0

    def on_terminate(self):
        # TODO: release resources
        pass

    def get_ability_port(self) -> int:
        return self.ability_port
```

### Generated main.py example

```python
from service import ImplAbility, task_manager
import ability_py
from task import RecognizeTask  # auto-generated import based on CR tasks


# register tasks (auto-generated based on the CR's taskType mapping)
tasks = {
    0: RecognizeTask(),
}

task_manager.register_tasks(tasks)

if __name__ == "__main__":
    ability = ImplAbility()
    service = ability_py.AbilityService()
    service.run(ability)
```

### Scaffold tool commands

```bash
# generate an ability project from a CR
python scaffold.py --cr ability.cr.yaml --output ./my-ability

# one-step from OpenAPI: generate CR + project
python scaffold.py --openapi MyAbility.openapi.yaml --output ./my-ability
```

### Key implementation

The scaffold tool (`scaffold.py`) reads the tasks definitions in the CR/Manifest:

1. Generates a Task class for each taskType (class name derived from summary or taskType)
2. Pre-fills parameter-unpacking code in execute() (`input_data.get("param_name", default)`)
3. Lists the returns fields in the docstring
4. Auto-registers all tasks in main.py
5. ability.py provides the standard lifecycle skeleton, leaving on_start/on_terminate as TODO
6. The service/ directory is copied from the SDK templates (server.py, interface.py, task_manager.py)

### Developers only need to focus on

1. **`on_start()` in `ability.py`** — initialization logic (loading models, connecting hardware, etc.)
2. **`execute()` of each Task in `task.py`** — business implementation (detection, planning, control, etc.)
3. **`on_terminate()` in `ability.py`** (optional) — resource release

All other code (main.py entry, Flask server, TaskManager, heartbeat, lifecycle responses) is provided by the scaffold + SDK.

---

## Phase 2: New Service resource type

### Core idea

An Ability is invoked on demand; a Service runs persistently. The framework adds a built-in `service.crd`, alongside `ability.crd`.

### Difference between Service and Ability

| Dimension | Ability | Service |
|------|---------|---------|
| Purpose | On-demand task execution (e.g. object detection, path planning) | Long-running service provision (e.g. video streaming, sensor collection) |
| Lifecycle | Inactive→Init→Standby→Running→Suspend→Terminated | Stopped→Starting→Running→Failed→Restarting |
| Exit behavior | Normal exit, notifies the parent ability | Auto-restarts per restartPolicy |
| Restart policy | none | always / on-failure / never |
| Health check | Heartbeat timeout only (1-minute passive check) | Active probing: HTTP liveness + readiness |
| SDK interface | on_start/on_connect/on_disconnect/on_terminate | on_start/on_stop + health endpoint |

### Service CRD (framework built-in)

```yaml
apiVersion: framework/v1
kind: CustomResourceDefinition
metadata:
  name: service.crd
spec:
  names:
    kind: Service
  schema:
    openAPIV3Schema:
      type: object
      required: [kind, metadata, spec]
      properties:
        kind:
          type: string
          enum: [Service]
        metadata:
          type: object
          required: [name]
          properties:
            name: { type: string }
            labels: { type: object, additionalProperties: { type: string } }
            annotations: { type: object, additionalProperties: { type: string } }
        spec:
          type: object
          required: [package, version, serviceName]
          properties:
            package: { type: string }
            version: { type: string }
            serviceName: { type: string }
            position: { type: string, default: "localhost" }
            config: {}
            restartPolicy:
              type: string
              enum: [always, on-failure, never]
              default: always
            maxRestarts: { type: integer, default: 5 }
            restartBackoffSeconds: { type: integer, default: 10 }
            healthCheck:
              type: object
              properties:
                liveness:
                  type: object
                  properties:
                    type: { type: string, enum: [http, process] }
                    path: { type: string }
                    port: { type: integer }
                    intervalSeconds: { type: integer, default: 10 }
                    failureThreshold: { type: integer, default: 3 }
                readiness:
                  type: object
                  properties:
                    type: { type: string, enum: [http] }
                    path: { type: string }
                    port: { type: integer }
                    intervalSeconds: { type: integer, default: 5 }
            devices: { type: array }
            models: { type: array }
```

### Service CR example

```yaml
kind: Service
metadata:
  name: camera-stream
spec:
  package: com.robot.camera
  version: 1.0.0
  serviceName: CameraStream
  position: localhost
  restartPolicy: always
  maxRestarts: 5
  restartBackoffSeconds: 10
  healthCheck:
    liveness:
      type: http
      path: /health
      port: 9090
      intervalSeconds: 10
      failureThreshold: 3
  config:
    resolution: "1080p"
    fps: 30
```

### Service package structure

```
packages/<name>/<version>/
  package.yaml                # kind: service (new kind field to distinguish)
  service.manifest.yaml       # service manifest
  bin/
    service                   # executable
```

`package.yaml` gains a `kind` field, defaulting to `ability` for backward compatibility.

### Restart policy implementation

```
process exits -> on_service_exit() callback
  |-- restartPolicy == never -> mark Stopped, do not restart
  |-- restartPolicy == on-failure && exit_code == 0 -> mark Stopped, do not restart
  +-- otherwise -> check restartCount < maxRestarts
      |-- yes -> wait backoff * 2^restartCount seconds then restart, mark Restarting
      +-- no -> mark Failed, do not restart again
```

### ability-py-sdk extension

New `ServiceInterface` base class:

```python
class ServiceInterface:
    def on_start(self):
        """Initialize and start the service"""
        pass

    def on_stop(self):
        """Stop the service"""
        pass

    def health_check(self) -> bool:
        """Return the service health status; called periodically by the framework"""
        return True

    def get_service_port(self) -> int:
        """Return the service listen port"""
        return 0
```

### Key code changes

| File | Change |
|------|------|
| New `include/resourcemgr/service_cr.hpp` | ServiceCR, ServiceSpec structs |
| New `include/lifecyclemgr/service_state.hpp` | ServiceState enum (Stopped/Starting/Running/Failed/Restarting) |
| New `src/lifecyclemgr/service_lifecycle.cpp` | Restart policy implementation, health-check probing logic |
| `src/lifecyclemgr/lifecycle_mgr.cpp` | on_service_exit() triggers the restart decision |
| `src/resourcemgr/resource_mgr.cpp` | ServiceCR add/delete/query, SQL_CREATE_SERVICE_CR_BASIC table |
| `src/resourcemgr/resource_mgr_http_apis.cpp` | New /api/service-cr, /api/service-heartbeat endpoints |
| `src/main.cpp` | Register the health-check timer |
| `ability-py-sdk/src/ability_py/` | New ServiceInterface, ServiceService |

### Backward compatibility

Phase 2 is purely additive and does not affect any existing Ability behavior.

---

## Phase 3: Generalization and unification

### 3.1 Resource type registry

Abstract a `ResourceTypeHandler` interface:

```cpp
class ResourceTypeHandler {
public:
    virtual ~ResourceTypeHandler() = default;
    virtual void validate_cr(const json& cr) = 0;
    virtual void on_start(const uuid& id) = 0;
    virtual void on_stop(const uuid& id) = 0;
    virtual void on_exit(const uuid& id, int exit_code) = 0;
    virtual void health_check(const uuid& id) = 0;
};
```

Ability and Service each implement a handler, looked up by kind. New resource types in the future only need to implement a handler + register a CRD.

### 3.2 Unified package format v3

```
packages/<name>/<version>/
  package.yaml          # name, version, arch, kind
  manifest.yaml         # unified manifest (distinguishes ability/service by kind)
  bin/
    main                # unified entry
```

### 3.3 Optional Ability keepAlive

Reusing Phase 2's restart infrastructure, an Ability can declare keepAlive:

```yaml
kind: AtomAbility
spec:
  keepAlive:
    enabled: true
    maxRestarts: 3
    restartBackoffSeconds: 15
```

### 3.4 Legacy format removal timeline

- Phase 3a: packages containing a legacy CRD emit a WARNING
- Phase 3b (next major version): remove the compatibility code that reads CRDs from packages
- Phase 3c: remove support for the v1 package format (crds/ directory)

---

## Implementation order

```
Phase 1 (CRD internalization):
  1. Create the AbilityManifest struct and serialization
  2. Load the framework CRD as an embedded resource
  3. read_package() supports reading ability.manifest.yaml
  4. Legacy CRD -> Manifest compatibility bridge
  5. Refactor the validation flow (json_schema_utils.cpp)
  6. Update the HTTP API
  7. Update the DB tables
  8. openapi-tool: rename the script, correct the concept, output CR + manifest
  9. Integration tests

Phase 1.5 (ability project scaffold):
  1. scaffold.py core logic: read the tasks definitions in the CR/Manifest
  2. Task class code generation (parameter unpacking, return comments, TODO placeholders)
  3. ability.py lifecycle skeleton generation
  4. main.py entry generation (auto-register tasks)
  5. Copy the service/ template directory from the SDK
  6. --openapi mode: integrate openapi-tool for a one-step flow
  7. End-to-end test: OpenAPI → CR → project → framework load & run

Phase 2 (Service resource type):
  1. ServiceCR/ServiceSpec structs
  2. Embed the Service CRD
  3. ServiceState enum and lifecycle logic
  4. Restart policy + health check
  5. Service HTTP API
  6. Service DB tables
  7. package.yaml kind field
  8. ability-py-sdk ServiceInterface
  9. Integration tests

Phase 3 (generalization):
  1. ResourceTypeHandler abstraction
  2. Unified package format
  3. Ability keepAlive
  4. Legacy format deprecation path
```

## Verification

### Phase 1

1. A new-format package (containing ability.manifest.yaml) can be loaded and have its CR validated correctly
2. A legacy-format package (containing ability.crd.yaml) is correctly converted through the compatibility bridge
3. The old and new validation paths produce consistent results
4. `GET /api/crd` returns the framework-level CRD
5. openapi-tool (after renaming) can correctly output the CR + manifest format

### Phase 1.5

1. Generate a project from a sample CR and confirm the directory structure and files are complete
2. The parameter-unpacking code in the generated task.py matches the CR's tasks definitions
3. After filling in task execute(), the project can be loaded and run by the framework normally
4. End-to-end --openapi mode: OpenAPI YAML → generate project → framework starts the ability → calling the task API succeeds

### Phase 2

1. Create a Service CR and verify the process starts
2. Kill the process and verify it auto-restarts per restartPolicy
3. After reaching maxRestarts, it is marked Failed
4. A failed health-check probe triggers a restart
5. restartPolicy: never verifies no restart on exit
6. ability-py-sdk ServiceInterface works correctly
