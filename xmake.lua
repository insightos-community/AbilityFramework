add_rules("mode.debug","mode.release")
add_languages("c++20")
set_policy("package.install_locally", true)
set_policy("package.requires_lock", true)

add_cxxflags("gcc::-fcoroutines","gcc::-pthread","gcc::-Werror=return-type")


add_requires("glog v0.7.1","yaml-cpp 0.8.0")
add_requires("nlohmann_json v3.12.0")

-- json-schema-validator is a dependency, so the version can be pinned
-- the json-schema-validator install process reads this environment variable
-- if the installed version is incorrect, run the command manually, then xmake config
-- export "NLOHMANN_JSON_VERSION", "3.12.0"
add_requireconfs("json-schema-validator.nlohmann_json", {version = "v3.12.0", override=true})

add_requires("uvw v3.4.0","cpp-httplib v0.18.7","cxxopts v3.3.1")
add_requires("json-schema-validator 2.4.0")
add_requires("stduuid v1.2.3",{configs = {span = true}})

add_requires("libcurl 8.11.0", {configs= {openssl3 = true}})
add_requires("jwt-cpp v0.7.1", {configs= {ssl = "openssl3"}})
add_requireconfs("openssl3", {version = "3.6.0"})
add_requireconfs("libuv", {version = "v1.48.0"})

add_requires("miniz 3.0.2",{configs = {cmake = true}})
add_requires("sqlitecpp 3.3.3")
add_requireconfs("sqlitecpp.sqlite3", {override=true})

add_includedirs("include")

-- if set, statically link the framework and all libraries
option("fwk-static")
    set_default(false)
    add_ldflags("-static")
    set_showmenu(true)
    set_description("statically link all libraries, used for cross-compilation")
option_end()

option("use-cpptrace")
    set_default(false)
    set_showmenu(true)
    set_description("use cpptrace, for framework debugging")
option_end()

if has_config("use-cpptrace") then
    add_requires("cpptrace v1.0.4")
end

option("enable-test")
    set_default(false)
    set_showmenu(true)
    set_description("use cpptrace, for framework debugging")
option_end()

-- Tests use a vendored doctest.h in test/ (no external package required)


target("AbilityFrameworkAux")
    set_kind("static")
    set_default(false)
    add_packages(
        "glog","nlohmann_json","yaml-cpp","openssl","cxxopts","sqlite3",
        -- "uvw","stduuid","json-schema-validator","hiredis","libcurl","miniz",
        "uvw","stduuid","json-schema-validator","libcurl","miniz",
        "cpp-httplib","jwt-cpp","sqlitecpp",{public=true})
    add_files("src/**.cpp")
    remove_files("src/main.cpp")
    add_options("with-cpptrace")
    if has_config("use-cpptrace") then
        add_packages("cpptrace", {public=true})
        add_defines("AFWK_USE_CPPTRACE", {public=true})
    end
    before_build(function(target)
        import("core.project.project")
        local projectdir = project.directory()
        cprint("${green}make versions")
        local config = {}
        local function set_var_from_cmd(varname, cmd)
            local out, err = os.iorun(cmd)
            config[varname] = string.trim(out)
            cprintf("${blue}  -- %s is %s\n", varname, config[varname])
        end
        set_var_from_cmd("@BUILD_DATE@", "date +%Y-%m-%d")
        set_var_from_cmd("@GIT_COMMIT_HASH@", "git -C " .. projectdir .. " rev-parse --short HEAD")

        local input_path = path.join(projectdir, "include/util/version.hpp.in")
        local output_path = path.join(projectdir, "include/version.hpp")
        local input_config = io.readfile(input_path)
        local output_config = input_config
        for k, v in pairs(config) do
            output_config = string.gsub(output_config, k, v)
        end
        cprint("${blue}  -- write to include/version.hpp")
        local version_unchanged = false
        if os.exists(output_path) then
            local current_output = io.readfile(output_path)
            if current_output == output_config then
                cprint("${green}output config already generated, no updates")
                version_unchanged = true
            end
        end
        if not version_unchanged then
            io.writefile(output_path, output_config)
            cprint("${green}write to include/version.hpp complete")
        end

        -- generate embedded WebUI (always runs; embed.py itself decides via mtime whether to rewrite)
        local embed_script = path.join(projectdir, "webui/embed.py")
        if os.exists(embed_script) then
            cprint("${green}embed webui")
            os.execv("python3", {embed_script})
        end
    end)

target("AbilityFramework")
    set_kind("binary")
    add_deps("AbilityFrameworkAux")
    add_files("src/main.cpp")
    add_options("fwk-static")

includes("test")

task("make-version")
    on_run(function()
        cprint("${green}make versions")
        local config = {}
        local function set_var_from_cmd(varname, cmd)
            local out,err = os.iorun(cmd)
            config[varname] = string.trim(out)
            cprintf("${blue}  -- %s is %s\n", varname, config[varname])
        end
        set_var_from_cmd("@BUILD_DATE@","date +%Y-%m-%d")
        set_var_from_cmd("@GIT_COMMIT_HASH@","git rev-parse --short HEAD")

        local input_config = io.readfile("include/util/version.hpp.in")
        local output_config = input_config
        for k, v in pairs(config) do
            output_config = string.gsub(output_config,k,v)
        end
        cprint("${blue}  -- write to include/version.hpp")
        if os.exists("include/version.hpp") then
            local current_output = io.readfile("include/version.hpp")
            if current_output == output_config then
                cprint("${green}output config alread generated, no updates")
                return
            end
        end
        io.writefile("include/version.hpp",output_config)
        cprint("${green}write to include/version.hpp complete")
    end)
    set_menu {
                -- settingmenu usage
                usage = "xmake make-version [options]"
                -- set the menu description
            ,   description = "generate include/version.hpp"

                -- set menu option; if no option, setting is {}
            ,   options = { }
            }
