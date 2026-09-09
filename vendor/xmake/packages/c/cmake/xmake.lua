-- Modified by InsightOS (2026): use public upstream downloads; retain pinned versions.
-- cmake — system-only recipe for the bundled recipe set.
--
-- Why this exists: packages.txt explicitly excludes build-only tools (cmake
-- / ninja / pkg-config) because they're expected to come from xmake's own
-- binary cache. Under --network=private that cache is never populated,
-- so xmake fails `package(cmake) not found!` as soon as any recipe declares
-- `package:add("deps", "cmake")` (e.g. miniz at xmake-io/xmake-repo).
--
-- This recipe lets xmake find cmake via `find_tool` — i.e. `cmake` on PATH.
-- Builder images (ability-builder-images/musl-builder) already bootstrap
--
-- No download / on_install path: if PATH lookup fails we want a loud error
-- pointing at the builder image rather than a silent attempt to fetch.
package("cmake")
    set_kind("binary")
    set_homepage("https://cmake.org")
    set_description("CMake build tool — bundled recipe: system-only fetch.")
    set_license("BSD-3-Clause")

    on_fetch(function (package, opt)
        return package:find_tool("cmake", {force = true})
    end)

    on_install(function (package)
        raise("cmake must be preinstalled on PATH when using the bundled recipe set " ..
              "(builder image is expected to bootstrap it). See packages.txt.")
    end)
