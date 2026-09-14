"""Check a native framework's HTTP/config/SQLite startup under a Unicode path.

The empty test instance has no abilities. Cleanup explicitly retires that test
process; it is not evidence for a live Robot's hold/stop protocol.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

source = Path(sys.argv[1]).resolve()
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
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
        database = root / "databases/ability_framework.db"
        retired = database.with_suffix(".retired")
        database.rename(retired)
        retired.rename(database)
print("PASS native HTTP/config/SQLite startup, Unicode paths, empty-instance restart")
