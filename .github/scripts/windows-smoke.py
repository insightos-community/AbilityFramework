"""Check a native framework's HTTP/config/SQLite startup under a Unicode path.

The empty test instance has no abilities. Cleanup explicitly retires that test
process; it is not evidence for a live Robot's hold/stop protocol.
"""
import json
import ctypes
from ctypes import wintypes as wt
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request


def loaded_modules(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    kernel.OpenProcess.restype = wt.HANDLE
    kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    psapi.EnumProcessModules.restype = wt.BOOL
    psapi.EnumProcessModules.argtypes = [wt.HANDLE, wt.LPVOID, wt.DWORD, ctypes.POINTER(wt.DWORD)]
    psapi.GetModuleFileNameExW.restype = wt.DWORD
    psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]
    handle = kernel.OpenProcess(0x410, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        modules = (wt.HMODULE * 2048)(); needed = wt.DWORD()
        if not psapi.EnumProcessModules(handle, modules, ctypes.sizeof(modules), ctypes.byref(needed)):
            raise ctypes.WinError(ctypes.get_last_error())
        assert needed.value <= ctypes.sizeof(modules)
        paths = []
        for module in modules[:needed.value//ctypes.sizeof(wt.HMODULE)]:
            buffer = ctypes.create_unicode_buffer(32768)
            if not psapi.GetModuleFileNameExW(handle, module, buffer, len(buffer)):
                raise ctypes.WinError(ctypes.get_last_error())
            paths.append(Path(buffer.value).resolve())
        return paths
    finally:
        kernel.CloseHandle(handle)

source = Path(sys.argv[1]).resolve()
reports = []
with tempfile.TemporaryDirectory(prefix="Semantic Windows ") as temporary:
    root = Path(temporary) / "中文 workspace with spaces"
    root.mkdir()
    binary = root / source.name
    shutil.copy2(source, binary)
    for dll in source.parent.glob("*.dll"):
        shutil.copy2(dll, root / dll.name)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    # JSON is a valid YAML subset; avoid adding a test-only PyYAML dependency.
    config = {"framework_name": "Windows 中文测试", "http_ip": "127.0.0.1", "http_port": port, "source_urls": [], "webui": {"enabled": False}}
    (root / "config.yaml").write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
    environment = {**os.environ, "ABILITY_FRAMEWORK_HOME": str(root)}
    environment['PATH'] = str(root) + os.pathsep + os.path.join(os.environ['SystemRoot'], 'System32')
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    for cycle in range(2):
        with (root / "process.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(binary), "-c", str(root / "config.yaml")], cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                ready = False
                for _ in range(100):
                    if process.poll() is not None:
                        raise RuntimeError((root / "process.log").read_text(encoding="utf-8", errors="replace"))
                    try:
                        with opener.open(f"http://127.0.0.1:{port}/api/hello", timeout=0.3) as response:
                            ready = response.status == 200
                        if ready:
                            break
                    except OSError:
                        time.sleep(0.1)
                assert ready, "Native framework did not become ready"
                with opener.open(f"http://127.0.0.1:{port}/api/config", timeout=1) as response:
                    assert json.load(response)["framework_name"] == config["framework_name"]
                assert (root / "databases/ability_framework.db").is_file()
                modules = loaded_modules(process.pid)
                crt = [path for path in modules if path.name.lower().startswith(('msvcp140', 'vcruntime140', 'concrt140'))]
                assert crt, 'No VC runtime module found in /MD application'
                assert all(path.parent == root.resolve() for path in crt), crt
                for path in modules:
                    assert path.is_relative_to(root.resolve()) or path.is_relative_to(Path(os.environ['SystemRoot']).resolve()), path
                reports.append({'cycle': cycle, 'loaded_modules': list(map(str, modules)), 'application_local_crt': True})
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
        database = root / "databases/ability_framework.db"
        retired = database.with_suffix(".retired")
        database.rename(retired)
        retired.rename(database)
(source.parent/'windows-validation.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')
print("PASS native HTTP/config/SQLite startup, Unicode paths, empty-instance restart")
