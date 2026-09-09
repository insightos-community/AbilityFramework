-- Modified by InsightOS (2026): use public upstream downloads; retain pinned versions.
-- ninja — system-only recipe for the bundled recipe set.
-- Same rationale as packages/c/cmake. Builder images install ninja via
-- apk / apt, so PATH lookup succeeds.
package("ninja")
    set_kind("binary")
    set_homepage("https://ninja-build.org/")
    set_description("Ninja build tool — bundled recipe: system-only fetch.")
    set_license("Apache-2.0")

    on_fetch(function (package, opt)
        return package:find_tool("ninja", {force = true})
    end)

    on_install(function (package)
        raise("ninja must be preinstalled on PATH when using the bundled recipe set. " ..
              "Builder images install it via apk / apt.")
    end)
