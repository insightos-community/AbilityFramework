-- Modified by InsightOS (2026): use public upstream downloads; retain pinned versions.
-- pkg-config — system-only recipe for the bundled recipe set.
-- Same rationale as packages/c/cmake. Builder images ship pkg-config
-- via apk (alpine) / apt (ubuntu), so PATH lookup succeeds.
package("pkg-config")
    set_kind("binary")
    set_homepage("https://www.freedesktop.org/wiki/Software/pkg-config/")
    set_description("pkg-config build tool — bundled recipe: system-only fetch.")
    set_license("GPL-2.0")

    on_fetch(function (package, opt)
        return package:find_tool("pkg-config", {force = true})
    end)

    on_install(function (package)
        raise("pkg-config must be preinstalled on PATH when using the bundled recipe set. " ..
              "Builder images install it via apk / apt.")
    end)
