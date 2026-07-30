// AbilityFramework WebUI
const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

let BASE = "http://localhost:8080";

async function api(method, path, body) {
  const opts = { method, headers: { "Content-Type": "application/json" } };
  if (body) opts.body = JSON.stringify(body);
  const res = await fetch(BASE + path, opts);
  const text = await res.text();
  try { return JSON.parse(text); } catch { return text; }
}

function jsonHtml(obj) {
  return typeof obj === "string" ? obj : JSON.stringify(obj, null, 2);
}

function stateClass(state) {
  const s = (state || "").toLowerCase();
  if (s === "running") return "state-running";
  if (s === "standby") return "state-standby";
  if (s === "inactive" || s === "stopped") return "state-inactive";
  if (s === "terminated") return "state-terminated";
  if (s === "error" || s === "failed") return "state-error";
  if (s === "completed") return "state-completed";
  return "";
}

// ===================== App =====================
const App = {
  async connect() {
    BASE = $("#framework-url").value.replace(/\/+$/, "");
    try {
      const r = await api("GET", "/api/hello");
      $("#conn-status").className = "status-dot online";
      $("#debug-hello").textContent = r;
      Debug.loadCrds();
      Debug.loadCrFiles();
      Templates.refresh();
      Instances.refresh();
      Lifecycle.refreshHeartbeats();
    } catch (e) {
      $("#conn-status").className = "status-dot offline";
      $("#debug-hello").textContent = "connection failed: " + e.message;
    }
  },
  showPanel(name) {
    $$(".panel").forEach((p) => p.classList.remove("active"));
    $$(".tab").forEach((t) => t.classList.remove("active"));
    $(`#panel-${name}`).classList.add("active");
    const tabs = $$(".tab");
    const map = { debug: 0, templates: 1, instances: 2, lifecycle: 3, tasks: 4, skills: 5 };
    tabs[map[name]].classList.add("active");
    if (name === "templates") {
      Templates.refresh();
      Templates.refreshDevices();
      Templates.refreshServices();
    }
    if (name === "instances") Instances.refresh();
    if (name === "lifecycle") Lifecycle.refreshHeartbeats();
    if (name === "tasks") Tasks.refreshAbilityList();
    if (name === "skills") Skills.refresh();
  },
};

// ===================== Debug =====================
const Debug = {
  async loadConfig() {
    const r = await api("GET", "/api/config");
    $("#debug-config").textContent = jsonHtml(r);
  },
  async loadCrds() {
    const crds = await api("GET", "/api/crd");
    const el = $("#debug-crds");
    if (!Array.isArray(crds) || crds.length === 0) {
      el.innerHTML = "<p>No CRDs</p>";
      return;
    }
    el.innerHTML = `<table><thead><tr><th>name</th><th>package name</th><th>version</th><th>Kind</th></tr></thead><tbody>` +
      crds.map((c) => `<tr><td>${c.metadata?.name || "-"}</td><td>${c.packageName || "-"}</td><td>${c.version || "-"}</td><td>${c.kind || "-"}</td></tr>`).join("") +
      `</tbody></table>`;
  },
  async loadPackages() {
    const r = await api("GET", "/api/package");
    $("#debug-packages").innerHTML = `<pre class="json-box">${jsonHtml(r)}</pre>`;
  },
  async loadCrFiles() {
    try {
      const r = await api("GET", "/api/crs");
      const el = $("#debug-cr-files");
      if (r.files && r.files.length > 0) {
        el.innerHTML = `<p>${r.total} files total</p><ul>` +
          r.files.map((f) => `<li>${f}</li>`).join("") + "</ul>";
      } else {
        el.innerHTML = "<p>No CR files</p>";
      }
    } catch { $("#debug-cr-files").innerHTML = "<p>-</p>"; }
  },
  async loadAlerts() {
    const r = await api("GET", "/api/ability-alert?latest=20");
    const el = $("#debug-alerts");
    if (Array.isArray(r) && r.length > 0) {
      el.innerHTML = `<pre class="json-box">${jsonHtml(r)}</pre>`;
    } else {
      el.innerHTML = "<p>No alerts</p>";
    }
  },
  async loadDiscovery() {
    try {
      const disc = await api("GET", "/api/discovery");
      const teams = await api("GET", "/api/team");
      const peers = await api("GET", "/api/team/peers");
      $("#debug-discovery").textContent = jsonHtml({ discovery: disc, teams, peers });
    } catch (e) {
      $("#debug-discovery").textContent = "load failed: " + e.message;
    }
  },
  async uploadPackage() {
    const fileInput = $("#pkg-upload-file");
    const force = $("#pkg-upload-force").checked;
    const resultEl = $("#pkg-upload-result");
    if (!fileInput.files || fileInput.files.length === 0) {
      resultEl.textContent = "please select zip file";
      return;
    }
    const file = fileInput.files[0];
    resultEl.textContent = `uploading... (${(file.size / 1024).toFixed(1)} KB)`;
    try {
      const formData = new FormData();
      // Force Content-Type to application/zip; the framework requires this type
      const zipFile = new File([file], file.name, { type: "application/zip" });
      formData.append("file", zipFile);
      const url = BASE + "/api/package" + (force ? "?force=true" : "");
      const res = await fetch(url, { method: "POST", body: formData });
      const text = await res.text();
      let data;
      try { data = JSON.parse(text); } catch { data = text; }
      if (res.ok) {
        resultEl.textContent = jsonHtml(data);
        Debug.loadPackages();
        Debug.loadCrds();
        // package-embedded CR already mirrored by the framework and reconcile; immediately refresh template/instance
        Templates.refresh();
        Instances.refresh();
      } else {
        resultEl.textContent = "upload failed (" + res.status + "): " + jsonHtml(data);
      }
    } catch (e) {
      resultEl.textContent = "upload failed: " + e.message;
    }
  },
};

// ===================== Templates (CR / Prototype) =====================
const Templates = {
  async refresh() {
    const crs = await api("GET", "/api/cr");
    const tbody = $("#template-table tbody");
    if (!Array.isArray(crs) || crs.length === 0) {
      tbody.innerHTML = '<tr><td colspan="7">No templates</td></tr>';
      return;
    }
    tbody.innerHTML = crs
      .filter((c) => c.kind !== "Device")
      .map((c) => {
        const name = c.metadata?.name || "-";
        const flag = (v) => (v === true ? "✓" : v === false ? "✗" : "-");
        return `<tr>
          <td>${name}</td>
          <td>${c.spec?.abilityName || "-"}</td>
          <td>${c.spec?.version || "-"}</td>
          <td>${flag(c.spec?.autoStart)}</td>
          <td>${flag(c.spec?.singleton)}</td>
          <td>${flag(c.spec?.keepAlive)}</td>
          <td>
            <button class="btn-sm" onclick="Templates.showDetail('${c.id}')">detail</button>
            <button class="btn-sm" onclick="Templates.startInstance('${name}')">start instance</button>
          </td>
        </tr>`;
      }).join("");
  },
  async refreshDevices() {
    const devs = await api("GET", "/api/cr?kind=device");
    const tbody = $("#device-table tbody");
    if (!Array.isArray(devs) || devs.length === 0) {
      tbody.innerHTML = '<tr><td colspan="4">No device CRs</td></tr>';
      return;
    }
    tbody.innerHTML = devs.map((d) => `<tr>
      <td>${d.metadata?.name || "-"}</td>
      <td>${d.spec?.deviceName || "-"}</td>
      <td>${d.spec?.version || "-"}</td>
      <td style="font-size:11px">${d.id || "-"}</td>
    </tr>`).join("");
  },
  async refreshServices() {
    try {
      const svcs = await api("GET", "/api/service-cr");
      const tbody = $("#service-table tbody");
      if (!Array.isArray(svcs) || svcs.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6">No service CRs</td></tr>';
        return;
      }
      tbody.innerHTML = svcs.map((s) => `<tr>
        <td>${s.metadata?.name || "-"}</td>
        <td>${s.spec?.serviceName || "-"}</td>
        <td>${s.spec?.version || "-"}</td>
        <td class="${stateClass(s.state)}">${s.state || "-"}</td>
        <td>${s.restartCount || 0}</td>
        <td style="font-size:11px">${s.id || "-"}</td>
      </tr>`).join("");
    } catch { $("#service-table tbody").innerHTML = '<tr><td colspan="6">-</td></tr>'; }
  },
  async showDetail(id) {
    const cr = await api("GET", `/api/cr/${id}`);
    $("#template-detail-json").textContent = jsonHtml(cr);
    $("#template-detail").style.display = "block";
  },
  async startInstance(templateName) {
    const r = await api("POST", "/api/instance", { template: templateName, start: true, connect: true });
    alert(jsonHtml(r));
    setTimeout(() => Instances.refresh(), 1500);
  },
};

// ===================== Instances (Runtime) =====================
let instanceTimer = null;
const Instances = {
  async refresh() {
    const list = await api("GET", "/api/instance");
    const tbody = $("#instance-table tbody");
    if (!Array.isArray(list) || list.length === 0) {
      tbody.innerHTML = '<tr><td colspan="7">No running instances</td></tr>';
      return;
    }
    tbody.innerHTML = list.map((i) => {
      const ts = i.start_time ? new Date(i.start_time * 1000).toLocaleString() : "-";
      return `<tr>
        <td>${i.instance_name || "-"}</td>
        <td>${i.ability_name || "-"}</td>
        <td>${i.cr_name || "-"}</td>
        <td class="${stateClass(i.state)}">${i.state || "-"}</td>
        <td>${ts}</td>
        <td style="font-size:11px">${i.instance_id || "-"}</td>
        <td>
          <button class="btn-sm" onclick="Instances.showDetail('${i.instance_id}')">detail</button>
          <button class="btn-sm btn-danger" onclick="Instances.destroy('${i.instance_id}')">terminate</button>
        </td>
      </tr>`;
    }).join("");
  },
  async showDetail(id) {
    const info = await api("GET", `/api/instance/${id}`);
    let hb = null;
    try { hb = await api("GET", `/api/ability-heartbeat/${id}`); } catch {}
    $("#instance-detail-json").textContent = jsonHtml({ instance: info, heartbeat: hb });
    $("#instance-detail").style.display = "block";
  },
  async destroy(id) {
    if (!confirm("Confirm terminate and destroy this instance?")) return;
    const r = await api("DELETE", `/api/instance/${id}`);
    alert(typeof r === "string" ? r : jsonHtml(r));
    setTimeout(() => Instances.refresh(), 1000);
  },
  toggleAutoRefresh() {
    if ($("#auto-refresh-instances").checked) {
      instanceTimer = setInterval(() => Instances.refresh(), 3000);
    } else {
      clearInterval(instanceTimer);
      instanceTimer = null;
    }
  },
};

// ===================== Lifecycle =====================
let hbTimer = null;
const Lifecycle = {
  async refreshHeartbeats() {
    const hbs = await api("GET", "/api/ability-heartbeat");
    const tbody = $("#heartbeat-table tbody");
    const select = $("#lc-instance-id");

    if (!Array.isArray(hbs) || hbs.length === 0) {
      tbody.innerHTML = '<tr><td colspan="6">no heartbeats</td></tr>';
      select.innerHTML = '<option value="">No instances</option>';
      return;
    }
    tbody.innerHTML = hbs.map((h) => `<tr>
      <td>${h.instanceName}</td>
      <td>${h.abilityName}</td>
      <td class="${stateClass(h.state)}">${h.state}</td>
      <td>${h.IPCPort}</td>
      <td>${h.abilityPort}</td>
      <td style="font-size:11px">${h.id}</td>
    </tr>`).join("");

    select.innerHTML = hbs.map((h) => `<option value="${h.id}">${h.instanceName} (${h.abilityName})</option>`).join("");
  },
  toggleAutoRefresh() {
    if ($("#auto-refresh-hb").checked) {
      hbTimer = setInterval(() => Lifecycle.refreshHeartbeats(), 3000);
    } else {
      clearInterval(hbTimer);
      hbTimer = null;
    }
  },
  async sendCommand() {
    const id = $("#lc-instance-id").value;
    const cmd = $("#lc-command").value;
    if (!id) { alert("please selectinstance"); return; }
    const r = await api("POST", "/api/lifecycle-request", {
      abilityInstanceId: id,
      command: cmd,
    });
    $("#lc-result").textContent = jsonHtml(r);
    setTimeout(() => Lifecycle.refreshHeartbeats(), 2000);
  },
};

// ===================== Tasks =====================
let selectedAbilityPort = null;

// Generate JSON skeleton default values based on manifest param.type
function defaultForType(type) {
  if (!type) return null;
  // Match custom types first
  if (type === "Point3d") return { x: 0, y: 0, z: 0 };
  if (type === "Quaternion") return { x: 0, y: 0, z: 0, w: 1 };
  const t = String(type).toLowerCase();
  if (t === "string") return "";
  if (t === "number" || t === "integer" || t === "int" || t === "float" || t === "double") return 0;
  if (t === "boolean" || t === "bool") return false;
  if (t === "array" || t === "list") return [];
  if (t === "object" || t === "dict" || t === "map") return {};
  return null;
}
function buildTaskInputSkeleton(task) {
  const obj = {};
  for (const p of (task && task.params) || []) {
    obj[p.name] = defaultForType(p.type);
  }
  return obj;
}
function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;",
  }[c]));
}

const Tasks = {
  // cache the last fetched manifest for syncing skeleton generation when task select switches
  currentManifest: null,

  async refreshAbilityList() {
    const hbs = await api("GET", "/api/ability-heartbeat");
    const select = $("#task-ability-select");
    if (!Array.isArray(hbs) || hbs.length === 0) {
      select.innerHTML = '<option value="">No running abilities</option>';
      return;
    }
    select.innerHTML = '<option value="">-- please select --</option>' +
      hbs.filter((h) => h.state === "Running" && h.abilityPort > 0)
        .map((h) => `<option value="${h.abilityPort}" data-id="${h.id}" data-ability="${escapeHtml(h.abilityName || "")}" data-version="${escapeHtml(h.version || "")}">${escapeHtml(h.instanceName)} (${escapeHtml(h.abilityName)}) - port ${h.abilityPort}</option>`)
        .join("");
  },
  async onAbilitySelected() {
    const select = $("#task-ability-select");
    selectedAbilityPort = select.value;
    const opt = select.options[select.selectedIndex];
    const reg = $("#task-registry");
    const taskSel = $("#task-type-select");
    const hint = $("#task-params-hint");
    Tasks.currentManifest = null;
    taskSel.innerHTML = '<option value="">-- select firstability --</option>';
    hint.innerHTML = "";
    if (!selectedAbilityPort) {
      reg.innerHTML = "";
      return;
    }
    // 1. Get runtime registered task index from the ability process's IPC port (for developer reference)
    try {
      const url = `${BASE.replace(/:\d+$/, "")}:${selectedAbilityPort}`;
      const res = await fetch(url + "/api/task/list/registry", { method: "POST" });
      const tasks = await res.json();
      if (Array.isArray(tasks) && tasks.length > 0) {
        reg.innerHTML = `<p style="font-size:12px;color:#888;margin-top:8px">registered Task:</p>
          <table><thead><tr><th>Index</th><th>Task Name</th></tr></thead><tbody>` +
          tasks.map((t) => `<tr><td>${t.index}</td><td>${escapeHtml(t.task_name)}</td></tr>`).join("") +
          `</tbody></table>`;
      } else {
        reg.innerHTML = "<p>No registered tasks</p>";
      }
    } catch (e) {
      reg.innerHTML = `<p style="color:#f44336">get task listfailure: ${e.message}</p>`;
    }
    // 2. Fetch manifest, fill task-type-select and schema hints with task definitions
    const ability = opt && opt.dataset.ability;
    const version = opt && opt.dataset.version;
    if (!ability || !version) { return; }
    try {
      const manifest = await api("GET", `/api/manifest/${encodeURIComponent(ability)}/${encodeURIComponent(version)}`);
      if (manifest && Array.isArray(manifest.tasks) && manifest.tasks.length > 0) {
        Tasks.currentManifest = manifest;
        taskSel.innerHTML = '<option value="">-- select task --</option>' +
          manifest.tasks.map(t =>
            `<option value="${t.taskType}">${t.taskType} - ${escapeHtml(t.taskName)}</option>`
          ).join("");
      } else {
        taskSel.innerHTML = '<option value="">manifest not declared task</option>';
      }
    } catch (e) {
      taskSel.innerHTML = `<option value="">manifest load failed</option>`;
    }
  },
  onTaskTypeChanged() {
    const v = $("#task-type-select").value;
    const hint = $("#task-params-hint");
    const ta = $("#task-input");
    if (!v || !Tasks.currentManifest) {
      ta.value = "{}";
      hint.innerHTML = "";
      return;
    }
    const taskType = parseInt(v);
    const task = (Tasks.currentManifest.tasks || []).find(t => t.taskType === taskType);
    if (!task) { return; }
    const skeleton = buildTaskInputSkeleton(task);
    ta.value = JSON.stringify(skeleton, null, 2);
    // Field hints: name/type/required/description, so users know how to fill in
    const lines = (task.params || []).map(p => {
      const opt = p.optional ? "?" : "";
      const desc = p.description ? ` — ${escapeHtml(p.description)}` : "";
      return `<div><code>${escapeHtml(p.name)}${opt}</code> <em>(${escapeHtml(p.type)})</em>${desc}</div>`;
    });
    if (Array.isArray(task.returns) && task.returns.length > 0) {
      lines.push("<div style=\"margin-top:4px;color:#6cf\">returns:</div>");
      for (const r of task.returns) {
        const desc = r.description ? ` — ${escapeHtml(r.description)}` : "";
        lines.push(`<div><code>${escapeHtml(r.name)}</code> <em>(${escapeHtml(r.type)})</em>${desc}</div>`);
      }
    }
    hint.innerHTML = lines.join("");
  },
  resetTaskInputSkeleton() {
    Tasks.onTaskTypeChanged();
  },
  async startTask() {
    if (!selectedAbilityPort) { alert("please select firstability"); return; }
    const v = $("#task-type-select").value;
    if (v === "") { alert("please select task"); return; }
    const taskType = parseInt(v);
    let input;
    try { input = JSON.parse($("#task-input").value); } catch (e) {
      $("#task-start-result").textContent = "JSON parse error: " + e.message;
      return;
    }
    try {
      const url = `${BASE.replace(/:\d+$/, "")}:${selectedAbilityPort}`;
      const res = await fetch(url + "/api/task/start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ task_type: taskType, input }),
      });
      const r = await res.json();
      $("#task-start-result").textContent = jsonHtml(r);
      if (r.task_id) {
        $("#task-query-id").value = r.task_id;
        // auto-query result
        setTimeout(() => Tasks.queryTaskStatus(), 2000);
      }
    } catch (e) {
      $("#task-start-result").textContent = "request failed: " + e.message;
    }
  },
  async queryTaskStatus() {
    if (!selectedAbilityPort) { alert("please select firstability"); return; }
    const taskId = $("#task-query-id").value;
    if (!taskId) { alert("Please enter a Task ID"); return; }
    try {
      const url = `${BASE.replace(/:\d+$/, "")}:${selectedAbilityPort}`;
      const res = await fetch(url + "/api/task/status", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ task_id: taskId }),
      });
      const r = await res.json();
      $("#task-status-result").textContent = jsonHtml(r);
    } catch (e) {
      $("#task-status-result").textContent = "request failed: " + e.message;
    }
  },
  async listTasks() {
    if (!selectedAbilityPort) { alert("please select firstability"); return; }
    try {
      const url = `${BASE.replace(/:\d+$/, "")}:${selectedAbilityPort}`;
      const res = await fetch(url + "/api/task/list", { method: "POST" });
      const tasks = await res.json();
      const tbody = $("#task-list-table tbody");
      if (!Array.isArray(tasks) || tasks.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5">No tasks</td></tr>';
        return;
      }
      tbody.innerHTML = tasks.map((t) => `<tr>
        <td style="font-size:11px">${t.task_id}</td>
        <td>${t.index}</td>
        <td class="${stateClass(t.status)}">${t.status}</td>
        <td style="font-size:11px">${t.created_at || "-"}</td>
        <td>
          <button class="btn-sm" onclick="$('#task-query-id').value='${t.task_id}';Tasks.queryTaskStatus()">view</button>
        </td>
      </tr>`).join("");
    } catch (e) {
      $("#task-list-table tbody").innerHTML = `<tr><td colspan="5">${e.message}</td></tr>`;
    }
  },
};

// ===================== Skills =====================
const Skills = {
  async refresh() {
    const list = await api("GET", "/api/skill");
    const tbody = $("#skill-table tbody");
    if (!Array.isArray(list) || list.length === 0) {
      tbody.innerHTML = '<tr><td colspan="6">No skill docs (add skills/*.md in the ability package zip to have them mirrored)</td></tr>';
      return;
    }
    tbody.innerHTML = list.map((s) => {
      const fn = encodeURIComponent(s.filename);
      return `<tr>
        <td>${s.package || "-"}</td>
        <td>${s.version || "-"}</td>
        <td><code>${s.filename || "-"}</code></td>
        <td>${s.title || "-"}</td>
        <td>${s.size || 0}</td>
        <td>
          <button class="btn-sm" onclick="Skills.view('${s.package}', '${s.version}', '${fn}')">view</button>
        </td>
      </tr>`;
    }).join("");
  },
  async view(pkg, ver, encodedFilename) {
    const url = `${BASE}/api/skill/${pkg}/${ver}/${encodedFilename}`;
    try {
      const res = await fetch(url);
      const text = await res.text();
      $("#skill-detail-body").textContent = text;
      $("#skill-detail").style.display = "block";
    } catch (e) {
      $("#skill-detail-body").textContent = "load failed: " + e.message;
      $("#skill-detail").style.display = "block";
    }
  },
};

// Auto-connect on page load
window.addEventListener("load", () => App.connect());
