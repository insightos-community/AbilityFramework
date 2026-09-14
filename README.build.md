# AbilityFramework: reproducible platform builds

## Versions, tools and build layout

The glibc installer pins component tag **v2.4.1-insightos.2026.2** at `d2dfd22410437f7fdbf72f247847e8fcab420ed9`.
This guide pins the current build-script snapshot at `b5e8e443d1a8f3911c19a43a3842dcbbe92b8cb1`.
To reconstruct another published release, read its `release.json` and select
both `source_commit` and `build_recipe_commit`; a source tag alone may predate
the CI scripts. This recipe reproduces the build steps, not historical archive bytes.

Prerequisites: Ubuntu 24.04 x86_64; xmake 3.1.1, GCC/G++, CMake, Ninja, pkg-config, Perl, Python 3, binutils, Autoconf, Automake and Libtool.

The release scripts expect **two sibling checkouts**, `automation/` for build
scripts and `source/` for the component. Run these commands from a fresh working
directory (the scripts themselves are not standalone copies):

```bash
REPRO_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/AbilityFramework-repro.XXXXXXXX")"
git clone --no-checkout https://github.com/insightos-community/AbilityFramework.git "$REPRO_ROOT/automation"
GIT_LFS_SKIP_SMUDGE=1 git -C "$REPRO_ROOT/automation" checkout --detach b5e8e443d1a8f3911c19a43a3842dcbbe92b8cb1
git clone --no-checkout https://github.com/insightos-community/AbilityFramework.git "$REPRO_ROOT/source"
GIT_LFS_SKIP_SMUDGE=1 git -C "$REPRO_ROOT/source" checkout --detach v2.4.1-insightos.2026.2
cd "$REPRO_ROOT/source"
test "$(git rev-parse HEAD)" = d2dfd22410437f7fdbf72f247847e8fcab420ed9
export TARGET_TAG=v2.4.1-insightos.2026.2
export COMPONENT=AbilityFramework
export GITHUB_SHA=b5e8e443d1a8f3911c19a43a3842dcbbe92b8cb1
```

## Linux glibc / standard component Release

The executable build entry is [`.github/scripts/build.sh`](.github/scripts/build.sh);
archive validation is [`.github/scripts/package.py`](.github/scripts/package.py).
From `source/` in the layout above:

```bash
bash ../automation/.github/scripts/build.sh
python3 ../automation/.github/scripts/package.py AbilityFramework
(cd .output/release && sha256sum -c SHA256SUMS)
```

Artifacts: `source/.output/release/` (archives/wheels, `release.json`, checksum
inventory and license notices). `release.json` records source and recipe revisions.
The local commands do not publish or overwrite a GitHub Release.

The script adds static dependency configuration to `source/xmake.lua`. Use the
disposable source checkout above. Full ELF static-link checks are performed by
the packager. A static C++ executable does not make the Python wheelhouse
independent of glibc.

## Linux musl

The current optional musl installer reuses the already-verified static
AbilityFramework binary from the base installer. A separate Alpine source build
entry also exists at [`ci/build-musl.sh`](ci/build-musl.sh). On a writable Alpine
checkout with xmake 3.1.1, a C++20 compiler, CMake, Ninja, pkg-config, Perl, Python 3,
binutils/file and static system libraries installed, run:

```bash
sh ci/build-musl.sh
readelf -lW build/linux/x86_64/release/AbilityFramework
readelf -dW build/linux/x86_64/release/AbilityFramework
```

This helper is an additional source-build route; the exact shipped musl installer
reproduction is the quick-start assembly recipe. Check that no interpreter or
NEEDED libraries remain before substituting the result.

For the complete musl build and offline checks, use the [quick-start musl commands](https://github.com/insightos-community/quick-start/blob/main/README.build.md#linux-musl-x86_64).

## macOS / macosx

Use Apple Silicon arm64 and the native adaptation at `b5e8e443d1a8f3911c19a43a3842dcbbe92b8cb1`;
the older glibc component tag above may not contain the macOS fixes. Start a
separate checkout and run the native commands:

```bash
git clone https://github.com/insightos-community/AbilityFramework.git AbilityFramework-macos
cd AbilityFramework-macos
git checkout --detach b5e8e443d1a8f3911c19a43a3842dcbbe92b8cb1
test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
```

Prerequisites: Xcode Command Line Tools, xmake 3.1.1, CMake, Ninja, pkg-config,
Perl and the Autoconf/Automake/Libtool toolchain used by xmake dependency recipes.

```bash
xmake f -y -p macosx -a arm64 -m release --fwk-static=n --enable-test=y --use-cpptrace=n
xmake build -vD -y -j 3 AbilityFramework
xmake build -y -j 3 test
xmake run test
build/macosx/arm64/release/AbilityFramework --version
otool -L build/macosx/arm64/release/AbilityFramework
mkdir -p .output
tar -czf .output/AbilityFramework-macos-arm64-development.tar.gz -C build/macosx/arm64/release AbilityFramework
```

The native workflow is [`.github/workflows/macos.yml`](.github/workflows/macos.yml).
Its artifacts are component development outputs; quick-start assembles and validates
the complete installer.

The complete macOS installer targets Apple Silicon/macOS 15.5+; see the [locked assembly instructions](https://github.com/insightos-community/quick-start/blob/main/README.build.md#macos-apple-silicon).

## GitHub workflow reproduction

The repository’s [CI workflow](.github/workflows/ci.yml) implements the two-checkout
layout. To build a source tag without publishing, create a reproduction branch at the
pinned automation commit. GitHub dispatch expects a branch/tag ref; both tag refs
and default-branch dispatches can enter this workflow’s publishing job. The following
commands require repository write access and use a non-default branch:

```bash
gh auth setup-git
REPRO_BRANCH=reproduce/platform-builds
git -C "$REPRO_ROOT/automation" push origin b5e8e443d1a8f3911c19a43a3842dcbbe92b8cb1:refs/heads/$REPRO_BRANCH
gh workflow run ci.yml --repo insightos-community/AbilityFramework --ref "$REPRO_BRANCH" -f tag=v2.4.1-insightos.2026.2
gh run list --repo insightos-community/AbilityFramework --workflow ci.yml --limit 5
# Set REPRO_RUN_ID to the selected run ID.
gh run watch "$REPRO_RUN_ID" --repo insightos-community/AbilityFramework --exit-status
gh run download "$REPRO_RUN_ID" --repo insightos-community/AbilityFramework --name release-assets --dir downloaded-release
```

```bash
git -C "$REPRO_ROOT/automation" push origin b5e8e443d1a8f3911c19a43a3842dcbbe92b8cb1:refs/heads/reproduce/macos
gh workflow run macos.yml --repo insightos-community/AbilityFramework --ref reproduce/macos
```

## Reproduction evidence

Build in a fresh checkout and a separate output directory for each ABI. Preserve
source commits, compiler/tool versions, dependency locks, package inventories and
test logs. Fixed source revisions and a container digest reproduce the recipe;
unlocked OS packages, runner images, timestamps and build tools can still change
archive bytes. Compare a downloaded release against its published `SHA256SUMS`;
do not expect a local rebuild to have the same digest.

See the [complete installer and repository index](https://github.com/insightos-community/quick-start/blob/main/README.build.md) for assembly order,
platform locks and end-to-end validation. Local build commands do not publish a
Release. Publishing requires repository write access and a new version tag;
existing release tags/assets should not be replaced.

## Windows x64 native development build

Install Visual Studio 2022 C++ tools/Windows SDK and xmake 3.1.1, then run from
this checkout in PowerShell:

```powershell
xmake f -y -p windows -a x64 -m release --fwk-static=n --enable-test=y --use-cpptrace=n
xmake build -y -j 3 AbilityFramework
xmake build -y -j 3 test
xmake run test
./build/windows/x64/release/AbilityFramework.exe --version
./.github/scripts/windows-stage.ps1
python .github/scripts/windows-smoke.py build/windows/x64/release/AbilityFramework.exe
```

The recipe is [.github/workflows/windows.yml](.github/workflows/windows.yml),
using `windows-2022` and the vendored dependency recipes. It uploads development
binaries after native build/tests. The current recipe uses the MSVC runtime;
`.github/scripts/windows-stage.ps1` supplies application-local redistributable
DLLs from the selected Visual Studio installation. The HTTP smoke clears the
build PATH and checks loaded module paths, including the local CRT. The archive
records runtime version/file hashes and the loaded-module validation report.
Compilation and unit tests alone do not qualify full Robot lifecycle or graphics.

Windows network discovery uses libuv interface enumeration and Windows route
selection. POSIX load averages are unavailable and reported as `unknown`, never
as a fabricated idle score. Windows Ability archives need a native
`bin/ability.exe` launcher from ability-scaffold and a bundled Python path in
`SEMANTIC_ABILITY_PYTHON`.
