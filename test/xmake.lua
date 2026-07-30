if has_config("enable-test") then
    target("test")
        set_default(false)
        add_deps("AbilityFrameworkAux")
        add_includedirs(".", {public = false})
        add_files("doctest-main.cpp")
        add_files("cases/**.cpp")
end
