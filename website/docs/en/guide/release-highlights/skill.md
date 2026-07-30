# Skill

Skill is a **core new feature** introduced by AbilityFramework. It lets an ability package carry AI-Agent-facing **skill documents** that are mirrored to a fixed location by the framework at install time and then exposed via HTTP API to the WebUI, MCP Server, and upper-layer integrators. Its goal is to make "how an ability package is called by an LLM" a first-class citizen of the framework with minimal intrusion.


::: info
The package author puts a `skills/` directory in the zip; the framework hosts it automatically, and the Agent can discover and read these documents.
:::


## The problem it solves

Before Skill, an ability package could only describe its own **runtime model** (CR/CRD: what parameters an ability has and what fields it returns), but could not describe **how to call it with an LLM** — for example, "which API to call first, how to fill the parameters, what a typical prompt looks like." This knowledge was scattered across wikis, READMEs, and even verbal agreements.

Skill turns this kind of knowledge into **document assets published with the package**: the package author writes it once, the framework hosts it uniformly, and all consumers (WebUI, MCP Server, Agent) discover it from the same fixed location without maintaining their own copies.

```mermaid
C4Context
    title Skill's place in the system

    Person(author, "Package author", "writes skills/*.md in the zip")
    Person(agent, "AI Agent", "needs to know how to call the ability")

    System_Boundary(fwk, "AbilityFramework") {
        System(res, "ResourceManager", "scans / mirrors in-package assets")
        System(store, "StoreManager", "skill mirroring + reading")
        System(api, "HTTP Server", "GET /api/skill")
        System(ui, "WebUI", "skill document panel")
    }

    System_Ext(pkg, "Ability package zip", "contains package.yaml + skills/")
    System_Ext(mcp, "MCP Server", "turns skills into tool definitions")
    SystemDb(fs, "File system", "skills/_packages/&lt;pkg&gt;/&lt;ver&gt;/")

    Rel(author, pkg, "write + package")
    Rel(pkg, store, "mirror on install")
    Rel(store, fs, "land mirrored files")
    Rel(res, store, "update() idempotent backfill")
    Rel(api, store, "list_all_skills / read_skill")
    Rel(ui, api, "GET /api/skill")
    Rel(mcp, api, "GET /api/skill")
    Rel(agent, mcp, "read tool docs")
    Rel(agent, ui, "manual review")
```

---

## Core concepts

| Concept | Description |
|---|---|
| **Skill file** | A text file (markdown / txt / json or any extension) describing how to use an ability that an Agent can call |
| **In-package skill** | A file under the `skills/` subdirectory of the ability package zip, published with the package |
| **Mirror** | At install time, recursively copies `skills/**` to `<framework_home>/skills/_packages/<pkg>/<version>/` |
| **SkillEntry** | Metadata of a single skill: package name, version, filename, size, title |
| **Consumers** | WebUI (manual review), MCP Server (turns into tool definitions), upper-layer Agent (decides how to call) |

Key design tradeoff: **Skills do not enter the database, do not participate in reconcile, and do not restrict file extensions.** They are pure "packaged document assets"; their lifecycle follows the ability package's install/uninstall entirely and is managed by the file system.

---

## Relationship between Skill and ability package

Skill is an **optional sub-asset** of an ability package. A package's structure looks like this:

```
my-ability-pkg/
├── package.yaml          # required: package metadata (name/version/arch)
├── bin/                  # ability executable
├── crs/                  # optional: in-package CR templates (*.yaml)
├── abilities/            # ability/device CRD definitions
└── skills/               # optional: in-package skill documents (NEW)
    ├── SKILL.md          # main skill description
    └── tasks/
        ├── move.md       # sub-task description (supports nested directories)
        └── grasp.md
```

The `skills/` directory can be any depth and any extension. The framework preserves the directory structure recursively when mirroring and locates files by relative path when reading.

```mermaid
classDiagram
    class AbilityPackage {
        +string name
        +string version
        +string arch
        +path install_dir
    }
    class SkillFile {
        +path relative_path
        +string content
    }
    class CRFile {
        +path path
    }
    class Executable {
        +path binary
    }

    AbilityPackage "1" o-- "0..*" SkillFile : skills/
    AbilityPackage "1" o-- "0..*" CRFile : crs/
    AbilityPackage "1" o-- "1..*" Executable : bin/

    note for SkillFile "any extension, any directory depth\nno DB, no reconcile"
    note for CRFile "only *.yaml\nmirrored into DB reconcile"
```

Both Skill and CR are "in-package assets", but their positioning is completely different; there is a dedicated comparison below.

---

## The mirroring mechanism

### Mirror directory layout

The framework mirrors all packages' skills uniformly under `$ABILITY_FRAMEWORK_HOME/skills/_packages/`, layered by `<package>/<version>`, preserving the original directory structure inside:

```
$ABILITY_FRAMEWORK_HOME/
└── skills/
    └── _packages/
        ├── mover/                       # package name
        │   ├── 1.0.0/                   # version
        │   │   ├── SKILL.md
        │   │   └── tasks/
        │   │       └── move.md
        │   └── 1.1.0/
        │       └── SKILL.md
        └── gripper/
            └── 2.0.0/
                └── SKILL.md
```

This layout is **machine-enumerable**: `list_all_skills()` first scans two layers of directories (package/version), then recursively grabs all regular files within each version directory. Consumers do not need to know how a package was installed; they only read from this fixed root.

### Mirror data flow

```mermaid
flowchart LR
    subgraph src["Ability package install dir (source of truth)"]
        P["packages/&lt;pkg&gt;/&lt;ver&gt;/"]
        SK["skills/SKILL.md"]
        SK2["skills/tasks/move.md"]
        P --- SK
        P --- SK2
    end

    subgraph img["Mirror dir (framework-hosted)"]
        D["skills/_packages/&lt;pkg&gt;/&lt;ver&gt;/"]
        MK["SKILL.md"]
        MK2["tasks/move.md"]
        D --- MK
        D --- MK2
    end

    SK -->|"copy_file\noverwrite_existing"| MK
    SK2 -->|"recursively preserve dir"| MK2

    MIRROR["mirror_package_skills\nrecursive copy, returns file count"]
    MIRROR -.->|"drives"| SK
    MIRROR -.->|"drives"| SK2
```

### Three mirror trigger points

Mirroring happens at **three points** in the ability package lifecycle, guaranteeing that no matter how a package arrives, its skills eventually land:

```mermaid
flowchart TD
    START["Ability package enters the system"] --> Q1{"Through which path?"}

    Q1 -->|"HTTP upload / FTP download"| EXTRACT["StoreManager::extract_package"]
    Q1 -->|"online install add_package"| ADD["StoreManager::add_package"]
    Q1 -->|"framework restart / periodic update"| UPD["ResourceManager::update"]

    EXTRACT --> M1["mirror_package_skills"]
    ADD --> M2["mirror_package_skills"]
    UPD --> M3["mirror_package_skills (idempotent backfill)"]

    M1 --> LAND["land at\nskills/_packages/&lt;pkg&gt;/&lt;ver&gt;/"]
    M2 --> LAND
    M3 --> LAND
```

| Trigger point | Function | Scenario | Idempotent |
|---|---|---|---|
| Extract install | `extract_package` | HTTP/FTP uploaded zip | yes (overwrite_existing) |
| Online install | `add_package` | `POST /api/package` online install | yes |
| Periodic update | `ResourceManager::update` | framework startup + timer; backfills mirroring for all existing packages | yes |

> The mirroring call inside `update()` is a **key fallback design**: the comment explicitly states "the install path already called this in add_package / extract_package, but for old packages that existed when the framework started (which never went through the install path), we backfill once here so the packages directory is the source of truth." This means that even if you manually copy a package into the `packages/` directory, the next update will automatically mirror its skills.

### Mirror function details

Behavior of `mirror_package_skills`:

1. Checks whether `<pkg_install_dir>/skills` exists and is a directory; if not, returns 0 directly
2. Creates the destination mirror directory
3. Uses `recursive_directory_iterator` to traverse all files under `skills/`
4. For each regular file, preserves the relative path, `copy_file` and overwrites the existing file
5. A single failed file copy only logs a WARNING and continues (does not abort the whole), and returns the count of successfully copied files

```mermaid
flowchart TD
    A["mirror_package_skills(pkg_dir, name, ver)"] --> B{"skills/ exists?"}
    B -- no --> R0["return 0"]
    B -- yes --> C["create_directories(dest_dir)"]
    C --> D{"success?"}
    D -- no --> R0b["return 0\nlog WARNING"]
    D -- yes --> E["recursive_directory_iterator(src_dir)"]
    E --> F{"more files?"}
    F -- yes --> G{"is regular file?"}
    G -- no --> F
    G -- yes --> H["compute rel = relative(path, src_dir)\ntarget = dest_dir / rel"]
    H --> I["create_directories(target.parent)"]
    I --> J["copy_file overwrite_existing"]
    J --> K{"success?"}
    K -- yes --> L["++count\nlog INFO"]
    K -- no --> M["log WARNING, ec.clear()"]
    L --> F
    M --> F
    F -- no --> N["return count"]
```

---

## Uninstall and cleanup

When an ability package is uninstalled, its mirrored skills are removed synchronously. `unmirror_package_skills` accepts an optional version number: passing a specific version deletes only that version, and passing an empty string deletes all versions of the package.

```mermaid
flowchart TD
    RP["StoreManager::remove_package(spec)"] --> DEL["remove_all(packages/&lt;pkg&gt;/&lt;ver&gt;)"]
    DEL --> UC["unmirror_package_crs"]
    DEL --> US["unmirror_package_skills(pkg, ver)"]
    US --> Q{"version empty?"}
    Q -- yes --> P1["path = skills/_packages/&lt;pkg&gt;\n(delete all versions of the package)"]
    Q -- no --> P2["path = skills/_packages/&lt;pkg&gt;/&lt;ver&gt;\n(delete only that version)"]
    P1 --> RM["remove_all(path)"]
    P2 --> RM
    RM --> Q2{"success?"}
    Q2 -- no --> W["log WARNING"]
    Q2 -- yes --> LOG["log INFO: removed N entries"]
```

Cleanup is strictly symmetric with mirroring: whatever was mirrored is removed on uninstall, leaving no orphan documents.

---

## Skill lifecycle

The complete state transitions of a skill file from authoring to disappearance:

```mermaid
stateDiagram-v2
    [*] --> Authored: author writes skills/*.md in the zip

    Authored --> Mirrored: mirrored when the package installs
    note right of Mirrored
        lands in the skills/_packages/ directory
    end note

    Mirrored --> Mirrored: update idempotent re-mirror
    Mirrored --> Readable: exists in the mirror directory

    Readable --> Listed: GET /api/skill list_all_skills
    Readable --> Served: GET /api/skill/{pkg}/{ver}/{file}
    Listed --> Served: consumer fetches raw content by metadata

    Readable --> Removed: package uninstall
    Mirrored --> Removed: package uninstall
    Removed --> [*]

    note left of Readable
        discoverable by WebUI / MCP / Agent
        no DB, no reconcile
    end note
```

---

## Install sequence

A complete chain of "upload an ability package with skills → mirror → readable":

```mermaid
sequenceDiagram
    participant U as Uploader
    participant API as HTTP Server
    participant SM as StoreManager
    participant FS as File system
    participant RES as ResourceManager

    U->>API: POST /api/package (zip)
    API->>SM: add_package(zip, data, force)
    SM->>FS: extract to a temp directory
    SM->>SM: read package.yaml to validate name/version/arch
    SM->>FS: rename to packages/{pkg}/{ver}/
    SM->>SM: mirror_package_crs(...)
    SM->>SM: mirror_package_skills(dest, pkg, ver)
    SM->>FS: recursively copy skills/** to the mirror dir
    SM-->>API: AddPackageRes installed=true
    API-->>U: 200 OK

    Note over RES: framework startup / periodic update()
    RES->>FS: scan packages/
    RES->>SM: idempotent mirror_package_skills for each package
    SM->>FS: overwrite-style backfill

    Note over U,API: now readable
    U->>API: GET /api/skill
    API->>SM: list_all_skills()
    SM->>FS: enumerate skills/_packages/**
    SM-->>API: SkillEntry list
    API-->>U: JSON metadata list
```

---

## HTTP API

Skills are exposed through two GET endpoints, both registered in `resource_mgr_http_apis.cpp`.

### `GET /api/skill` — list all skill metadata

Returns a JSON array where each item is a `SkillEntry`. It traverses all packages/versions under `skills/_packages/` and recursively grabs files.

Response example:

```json
[
  {
    "package": "mover",
    "version": "1.0.0",
    "filename": "SKILL.md",
    "size": 1234,
    "title": "Movement ability usage guide"
  },
  {
    "package": "mover",
    "version": "1.0.0",
    "filename": "tasks/move.md",
    "size": 567,
    "title": "Linear move task"
  }
]
```

### `GET /api/skill/:package/:version/*filename` — read a single skill's raw content

Uses the wildcard regex `([^/]+)/([^/]+)/(.+)` to capture the package name, version, and filename (the filename may include subdirectories, e.g. `tasks/move.md`). Returns the raw `text/markdown; charset=utf-8` content. Returns 404 + a JSON error body if not found.

```
GET /api/skill/mover/1.0.0/SKILL.md           → 200 text/markdown
GET /api/skill/mover/1.0.0/tasks/move.md      → 200 text/markdown
GET /api/skill/mover/1.0.0/nope.md            → 404 {"error":"skill not found",...}
GET /api/skill/mover/1.0.0/../../etc/passwd   → 404 (path traversal blocked)
```

### Call relationship between the API and StoreManager

```mermaid
flowchart TD
    REQ["HTTP GET /api/skill..."] --> ROUTE{"route match?"}
    ROUTE -->|"/api/skill"| L["list_all_skills"]
    ROUTE -->|"/api/skill/:p/:v/:f"| R["read_skill(pkg,ver,file)"]

    L --> ENUM["directory_iterator two layers: pkg/version"]
    ENUM --> REC["recursive_directory_iterator grab files"]
    REC --> TITLE["extract_skill_title extract title"]
    TITLE --> JSON["assemble SkillEntry JSON array"]

    R --> CANON["weakly_canonical(base / filename)"]
    CANON --> GUARD{"falls within base?"}
    GUARD -- no --> NUL["return nullopt"]
    GUARD -- yes --> EXIST{"is regular file?"}
    EXIST -- no --> NUL
    EXIST -- yes --> READ["read full UTF-8"]
    READ --> TXT["return content"]

    JSON --> RESP["200 application/json"]
    TXT --> RESP2["200 text/markdown"]
    NUL --> RESP3["404 + JSON error"]
```

---

## Path traversal protection

`read_skill` has an explicit path traversal guard. Because the filename comes from external HTTP input, it must prevent attacks like `../../etc/passwd` from reading arbitrary files.

```mermaid
flowchart TD
    IN["input pkg, version, filename"] --> EMPTY{"any empty?"}
    EMPTY -- yes --> NUL["return nullopt"]
    EMPTY -- no --> BASE["base = mirrored_pkg_skills_dir(pkg, ver)"]
    BASE --> T["target = weakly_canonical(base / filename)"]
    T --> BC["base_canon = weakly_canonical(base)"]
    BC --> REL["rel = relative(target, base_canon)"]
    REL --> CHK{"rel empty\nor starts with ..?"}
    CHK -- yes --> NUL
    CHK -- no --> E2{"exists and is regular file?"}
    E2 -- no --> NUL
    E2 -- yes --> OUT["read full content and return"]
```

Only when the resolved canonical path **strictly falls within the mirror directory** is reading allowed. Even if `..` is stuffed into the package name/version, it is intercepted after canonicalization.

---

## Title extraction

The `title` field returned by `list_all_skills` is generated by `extract_skill_title`, which tries to grab a readable title from the first 60 lines of the skill file so that consumers can display it without reading the whole file. Extraction order:

```mermaid
flowchart TD
    OPEN["open file"] --> L1{"line 1 == --- ?"}
    L1 -- yes --> FM["enter YAML frontmatter mode"]
    L1 -- no --> HEADING["scan body"]

    FM --> SCANFM["read line by line, grab name: / title:"]
    SCANFM --> ENDFM{"hit second --- ?"}
    ENDFM -- no --> SCANFM
    ENDFM -- yes --> HEADING

    HEADING --> H1{"line starts with #?"}
    H1 -- yes --> H1RES["return that level-1 heading text"]
    H1 -- no --> MORE{"read 60 lines?"}
    MORE -- no --> HEADING
    MORE -- yes --> FMRES["prefer frontmatter title\nthen name"]
    FMRES --> FINAL{"both empty?"}
    FINAL -- yes --> EMPTY["return empty string"]
    FINAL -- no --> RET["return the captured title"]
```

In other words, the priority is: **the first `#` heading in the body > frontmatter `title:` > frontmatter `name:` > empty string**. The value is trimmed of leading/trailing whitespace and surrounding quotes. This covers the two mainstream markdown skill writing styles (with and without frontmatter).

---

## StoreManager's Skill interface

```mermaid
classDiagram
    class StoreManager {
        <<static members>>
        +mirror_package_skills(pkg_dir, name, ver) size_t
        +unmirror_package_skills(name, ver) void
        +mirrored_pkg_skills_dir(name, ver) path
        +list_all_skills() vector~SkillEntry~
        +read_skill(pkg, ver, filename) optional~string~
    }

    class SkillEntry {
        +string package
        +string version
        +string filename
        +size_t size_bytes
        +string title
    }

    class extract_skill_title {
        <<namespace anonymous>>
        +extract_skill_title(path) string
    }

    StoreManager ..> SkillEntry : list_all_skills returns
    StoreManager ..> extract_skill_title : internal call
    note for SkillEntry "filename is relative to &lt;pkg&gt;/&lt;ver&gt;/\ne.g. SKILL.md or tasks/move.md"
```

| Method | Purpose | Caller |
|---|---|---|
| `mirror_package_skills` | Recursively copy skills/ to the mirror dir | extract / add / update |
| `unmirror_package_skills` | Delete mirror (supports deleting only a version) | remove_package |
| `mirrored_pkg_skills_dir` | Compute the fixed mirror path | internal / diagnostics / tests |
| `list_all_skills` | Enumerate all skill metadata | `GET /api/skill` |
| `read_skill` | Read a single skill's raw content (with traversal guard) | `GET /api/skill/:p/:v/:f` |

---

## Skill vs CR mirroring

Skill mirroring and CR mirroring are **isomorphic** designs (the same mirror/unmirror/mirrored_dir trio, the same three trigger points), but their semantics differ completely. Understanding this pair helps you quickly grasp the framework's overall approach to "in-package assets".

```mermaid
block-beta
    columns 4

    block:pkg["Ability package install dir"]
        crs["crs/*.yaml"]
        skills["skills/**"]
    end

    block:mirror["Framework mirror layer"]
        m1["mirror_package_crs"]
        m2["mirror_package_skills"]
    end

    block:store["Landing"]
        s1["crs/_packages/..."]
        s2["skills/_packages/..."]
    end

    block:consume["Consumption layer"]
        c1["read_crs_into_db\nreconcile takes effect"]
        c2["list_all_skills\nread_skill"]
    end

    crs --> m1 --> s1 --> c1
    skills --> m2 --> s2 --> c2
```

| Dimension | CR mirror | Skill mirror |
|---|---|---|
| Extension | only `*.yaml` | any (md/txt/json/...) |
| Enters DB | yes, `read_crs_into_db` parses into the DB | no, pure file asset |
| Participates in reconcile | yes, `update()` keeps/deletes CR rows | no, unrelated to the runtime model |
| Directory layering | `crs/_packages/<pkg>/<ver>/` | `skills/_packages/<pkg>/<ver>/` |
| Consumption | in-process DB query | HTTP API + file read |
| Positioning | runtime model (ability params/returns) | Agent documentation (how to call) |

In one sentence: CR describes "what the ability looks like", and Skill describes "how to use the ability". The former is for the framework and scheduler; the latter is for the LLM.

---

## Consumer perspective

### WebUI

The WebUI has a dedicated "Skill documents" panel (`panel-skills`) that lists all skills (package, version, file, title, size) in a table and reads the raw content on "view".

```mermaid
sequenceDiagram
    participant User as User
    participant UI as WebUI skill panel
    participant API as HTTP Server
    participant SM as StoreManager

    User->>UI: switch to the "Skill documents" tab
    UI->>API: GET /api/skill
    API->>SM: list_all_skills()
    SM-->>API: SkillEntry list
    API-->>UI: JSON metadata
    UI->>UI: render table
    User->>UI: click "view" on a row
    UI->>API: GET /api/skill/{pkg}/{ver}/{file}
    API->>SM: read_skill(pkg, ver, file)
    SM-->>API: skill raw content
    API-->>UI: text/markdown
    UI->>UI: show raw content in a pre tag
```

### MCP Server / Agent

The MCP Server is the main automated consumer of skills: it turns skill documents into tool definitions that the LLM can understand, and the Agent decides when and how to call the corresponding ability based on them.

```mermaid
sequenceDiagram
    participant Agent as AI Agent
    participant MCP as MCP Server
    participant API as Framework HTTP
    participant Ability as Ability process

    Agent->>MCP: need to perform a "move" task
    MCP->>API: GET /api/skill
    API-->>MCP: skill metadata list
    MCP->>MCP: match mover/SKILL.md
    MCP->>API: GET /api/skill/mover/1.0.0/SKILL.md
    API-->>MCP: raw call instructions
    MCP->>MCP: parse into tool parameter definitions
    MCP-->>Agent: tool schema + usage
    Agent->>Agent: generate call parameters
    Agent->>API: POST /api/instance (as described by the skill)
    API->>Ability: start ability
    Ability-->>API: heartbeat ready
    API-->>Agent: instance_id
```

### Manual vs automated discovery comparison

```mermaid
flowchart LR
    subgraph write["Author side"]
        A["write skills/*.md"]
    end

    subgraph mgmt["Framework hosting"]
        M["mirror to a fixed directory"]
        E["GET /api/skill enumerate"]
    end

    subgraph consume["Consumption side"]
        H["WebUI\nmanual browsing"]
        MC["MCP Server\nauto-convert to tool"]
        AG["Agent\ndecide on call"]
    end

    A --> M --> E
    E --> H
    E --> MC
    H -.->|"read raw"| READ1["GET /api/skill/:f"]
    MC -.->|"read raw"| READ1
    MC --> AG
    READ1 --> AB["actually call the ability"]
    AG --> AB
```

---

## Full lifecycle overview

Connecting all the pieces, the end-to-end flow of a skill document from creation to disappearance:

```mermaid
flowchart TD
    A["author writes skills/*.md"] --> B["package into zip"]
    B --> C{"how does the package enter the framework?"}

    C -->|"HTTP/FTP"| D1["extract_package"]
    C -->|"online install"| D2["add_package"]
    C -->|"manually placed in packages/"| D3["(await update)"]

    D1 --> E["mirror_package_skills"]
    D2 --> E
    D3 --> F["ResourceManager::update\nidempotent backfill"]
    F --> E

    E --> G["land at skills/_packages/&lt;pkg&gt;/&lt;ver&gt;/"]
    G --> H["discoverable"]

    H --> H1["GET /api/skill list metadata"]
    H --> H2["GET /api/skill/:f read raw"]
    H --> H3["WebUI panel display"]
    H --> H4["MCP Server convert to tool"]

    G --> I{"package uninstalled?"}
    I -- no --> J["persistently available"]
    I -- yes --> K["remove_package"]
    K --> L["unmirror_package_skills"]
    L --> M["delete mirror directory"]
    M --> N["404 no longer visible"]
```

---

## Writing a Skill file

Package authors only need to place markdown files under `skills/` at the zip root. The framework does not mandate a specific content format, but both mainstream writing styles have their titles extracted correctly.

**Style 1: heading only**

```markdown
# Movement ability usage guide

This ability supports moving the robot to a given coordinate. First call ...
```

**Style 2: with frontmatter** (using three equals fences instead of the yaml separator as an example)

```text
name: move_skill
title: Linear move task
---
# Linear move task

Create an instance via POST /api/instance; the velocity parameter is in m/s ...
```

In both styles, `extract_skill_title` grabs a title (style 2 prefers the frontmatter `title:`). It is recommended to give each skill at least a clear title and a "how to call" section so the Agent can understand it.

Related references:

- [Module documentation overview](/en/guide/concepts/architecture)
- [Resource manager](/en/guide/concepts/architecture)
- [HTTP API reference](/en/api/http-api)
- [Message bus](/en/guide/concepts/architecture)
- [Directory structure](/en/guide/concepts/architecture)
