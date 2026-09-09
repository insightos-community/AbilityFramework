-- Copyright 2026 InsightOS
-- SPDX-License-Identifier: Apache-2.0
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

add_rules("mode.debug","mode.release")
add_languages("c++20")
set_policy("package.install_locally", true)
set_policy("package.requires_lock", true)

add_cxxflags("gcc::-fcoroutines","gcc::-pthread","gcc::-Werror=return-type")

-- Snapshot the release's dependency recipes locally; no private service is needed.
add_repositories("bundled " .. path.join(os.scriptdir(), "vendor", "xmake"))

add_requires("glog v0.7.1","yaml-cpp 0.8.0")
add_requires("nlohmann_json v3.12.0")

-- 其为json-schema-validator的依赖, 这样可以固定版本
-- json-schema-validator 的安装过程会读取此环境变量
-- 如果安装版本不对, 请手动执行命令, 再 xmake config
-- export "NLOHMANN_JSON_VERSION", "3.12.0"
add_requireconfs("json-schema-validator.nlohmann_json", {version = "v3.12.0", override=true})

add_requires("uvw v3.4.0","cpp-httplib v0.18.7","cxxopts v3.3.1")
add_requires("json-schema-validator 2.4.0")
add_requires("stduuid v1.2.3",{configs = {span = true}})

add_requires("libcurl 8.11.0", {configs= {openssl3 = true}})
add_requires("jwt-cpp v0.7.1", {configs= {ssl = "openssl3"}})
add_requireconfs("**.openssl3", {version = "3.6.0", override = true, system = false})
add_requireconfs("libuv", {version = "v1.48.0"})

add_requires("miniz 3.0.2",{configs = {cmake = true}})
add_requires("sqlitecpp 3.3.3")
add_requireconfs("**.sqlite3", {override=true, version = "3.45.0+300", system = false})

add_includedirs("include")

-- 如果是,则在链接框架时以全静态方式链接所有库
option("fwk-static")
    set_default(false)
    add_ldflags("-static")
    set_showmenu(true)
    set_description("以静态方式链接所有库,用于交叉编译")
option_end()

option("use-cpptrace")
    set_default(false)
    set_showmenu(true)
    set_description("使用cpptrace, 用于框架debug")
option_end()

if has_config("use-cpptrace") then
    add_requires("cpptrace v1.0.4")
end

option("enable-test")
    set_default(false)
    set_showmenu(true)
    set_description("使用cpptrace, 用于框架debug")
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

        -- 生成嵌入式 WebUI (始终运行，由 embed.py 自身根据 mtime 决定是否重写)
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
                -- 设置菜单用法
                usage = "xmake make-version [options]"
                -- 设置菜单描述
            ,   description = "generate include/version.hpp"

                -- 设置菜单选项，如果没有选项，可以设置为{}
            ,   options = { }
            }
