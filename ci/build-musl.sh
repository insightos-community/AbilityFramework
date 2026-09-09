#!/bin/sh
# Copyright 2026 InsightOS
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Run inside an Alpine build environment with xmake >= 3.0.8, a C++20
# compiler, cmake, ninja, pkg-config, perl, python3 and static system libraries.
# Sources and recipes are included in this checkout; downloads use public URLs.
set -eu
export XMAKE_ROOT=y
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
command -v xmake >/dev/null
command -v cmake >/dev/null
command -v ninja >/dev/null
command -v pkg-config >/dev/null

# Do not rewrite source files or mutate the user's global configuration.
xmake f -m release --fwk-static=y --use-cpptrace=n -cvy
xmake b -j"$(getconf _NPROCESSORS_ONLN)" -y
case "$(uname -m)" in
  x86_64) arch=x86_64 ;;
  aarch64) arch=arm64 ;;
  *) echo "unsupported host architecture" >&2; exit 1 ;;
out="build/linux/$arch/release/AbilityFramework"
test -f "$out"
file "$out"
if readelf -d "$out" | grep -q '(NEEDED)'; then
  echo "Expected a fully static executable, but dynamic dependencies remain" >&2
  exit 1
fi
