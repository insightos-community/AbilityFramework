#!/usr/bin/env python3
# Copyright 2026 InsightOS
# SPDX-License-Identifier: Apache-2.0
"""Package only build outputs and record the exact source and CI recipe revisions."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

component = sys.argv[1]
source = Path.cwd()
out = source / ".output/release"
out.mkdir(parents=True, exist_ok=True)
sha = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
tag = os.environ.get("TARGET_TAG", "")
if tag:
    tagged = subprocess.check_output(["git", "rev-parse", f"refs/tags/{tag}^{{commit}}"], text=True).strip()
    if tagged != sha:
        raise SystemExit("Source checkout does not match the requested tag")
version = tag or "ci-" + sha[:12]
platform = "any" if component in ("semantic-web", "ability-py") else "linux-x86_64"
metadata = {
    "component": component, "tag": tag, "source_commit": sha,
    "build_recipe_commit": os.environ.get("GITHUB_SHA", ""),
    "platform": platform, "runner": "ubuntu-24.04",
    "workflow_run": f"https://github.com/{os.environ.get('GITHUB_REPOSITORY', '')}/actions/runs/{os.environ.get('GITHUB_RUN_ID', '')}",
    "validation_scope": "Component tests and package smoke checks; excludes GPU, hardware and full-stack acceptance.",
}
payload = source / ".output/payload"
payload.mkdir(parents=True, exist_ok=True)
for name in ("LICENSE", "NOTICE", "LICENSE_SCOPE.md"):
    if (source / name).is_file():
        shutil.copy2(source / name, payload / name)
if component == "ability-py":
    import tomllib
    import zipfile
    metadata["package_version"] = tomllib.loads((source / "pyproject.toml").read_text())["project"]["version"]
    wheels = list((source / "dist").glob("*.whl"))
    sdists = list((source / "dist").glob("*.tar.gz"))
    if len(wheels) != 1 or len(sdists) != 1:
        raise SystemExit("Expected exactly one wheel and one source distribution")
    with zipfile.ZipFile(wheels[0]) as wheel:
        if wheel.testzip() is not None or not any(n.endswith(".dist-info/RECORD") for n in wheel.namelist()):
            raise SystemExit("Invalid wheel")
    for file in wheels + sdists:
        shutil.copy2(file, out / file.name)
    for file in payload.iterdir():
        shutil.copy2(file, out / file.name)
else:
    if component == "semantic-web":
        if not (source / "dist/index.html").is_file():
            raise SystemExit("Missing production index.html")
        shutil.copytree(source / "dist", payload / "web", dirs_exist_ok=True)
        metadata["backend_routing"] = "Serve web/ with same-origin /api HTTP and /ws WebSocket reverse proxies to Semantic Server."
    else:
        names = ["AbilityFramework"] if component == "AbilityFramework" else ["semantic-server", "semantic-pilot", "semantic"]
        (payload / "bin").mkdir(exist_ok=True)
        metadata["binaries"] = {}
        for name in names:
            binary = source / ("build/linux/x86_64/release/AbilityFramework" if name == "AbilityFramework" else ".output/bin/" + name)
            headers = subprocess.check_output(["readelf", "-hW", str(binary)], text=True)
            program = subprocess.check_output(["readelf", "-lW", str(binary)], text=True)
            dynamic = subprocess.check_output(["readelf", "-dW", str(binary)], text=True)
            if "Advanced Micro Devices X86-64" not in headers or "INTERP" in program or "(NEEDED)" in dynamic:
                raise SystemExit(f"Expected a static x86_64 ELF: {name}")
            metadata["binaries"][name] = {"sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "static": True}
            shutil.copy2(binary, payload / "bin" / name)
    (payload / "release.json").write_text(json.dumps(metadata, indent=2) + "\n")
    archive = out / f"{component}-{version}-{platform}.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        for file in sorted(payload.iterdir()):
            tar.add(file, arcname=file.name)
(out / "release.json").write_text(json.dumps(metadata, indent=2) + "\n")
checksums = []
for file in sorted(out.iterdir()):
    if file.name != "SHA256SUMS":
        checksums.append(f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.name}")
(out / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
print(json.dumps(metadata, indent=2))
