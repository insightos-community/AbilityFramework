#!/usr/bin/env bash
# Copyright 2026 InsightOS
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
# Same release linkage policy as quick-start/artifacts/build_native.py.
# Only the build configuration is adapted; tagged application sources stay intact.
python3 - <<'PYTHON'
from pathlib import Path
path = Path("xmake.lua")
path.write_text('add_requireconfs("**", {system = false, configs = {shared = false}})\n' + path.read_text())
PYTHON
xmake f -y -m release --fwk-static=y --enable-test=y --use-cpptrace=n
xmake build -y -j 2 AbilityFramework
xmake build -y -j 2 test
xmake run test
build/linux/x86_64/release/AbilityFramework --version
