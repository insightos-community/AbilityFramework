// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "json_schema_utils.hpp"
#include "resourcemgr/builtin_crds.hpp"
#include "resourcemgr/ability_cr.hpp"
#include "resourcemgr/ability_manifest.hpp"
#include "util/expected.hpp"
#include "util/global_vars.hpp"
#include <glog/logging.h>
#include <nlohmann/json-schema.hpp>
#include <unordered_set>
#include <util/scope.hpp>
#include <util/yaml_to_json.hpp>

using Path = std::filesystem::path;

bool extension_is_yaml(const Path& p) {
    auto ext_name = p.extension().string();
    return ext_name.ends_with("yaml") || ext_name.ends_with("yml");
}

expected<YAML::Node, std::string> read_yaml_from_path(const Path& p) {
    if (!extension_is_yaml(p)) { return make_unexpected(p, " is not a yaml"); }
    try {
        auto node = YAML::LoadFile(p.string());
        return node;
    }
    catch (const std::exception& e) {
        return make_unexpected("error reading ", p, " : ", e.what());
    }
    catch (...) {
        return make_unexpected("error reading ", p, " : unknown error");
    }
}
// 标准校验
expected<void, ErrorMsg> validate_to_expected(
    const nlohmann::json& target, const nlohmann::json& schema
) {
    nlohmann::json_schema::json_validator validator;
    try {
        validator.set_root_schema(schema);
        validator.validate(target);
        return {};
    }
    catch (const std::exception& e) {
        LOG(WARNING) << "in validating target:\n" << target.dump(2);
        LOG(WARNING) << "with schema:\n" << schema.dump(2);
        LOG(ERROR) << e.what();
        return make_unexpected("validated failed: ", e.what());
    }
}

// 根据组合能力cr获取子能力的 manifest schema (从 ability.manifest.yaml 读取)
nlohmann::json get_sub_ability_crd(const AbilityCR& cr, const int number) {
    auto pkg_path = global_vars::home_path() / "packages"
                  / cr.spec->subabilities[number]->package
                  / to_string(cr.spec->subabilities[number]->version);
    auto manifest_path = pkg_path / "ability.manifest.yaml";
    if (!exists(manifest_path)) { return {}; }
    auto manifest_yaml = read_yaml_from_path(manifest_path);
    if (!manifest_yaml) { LOG(ERROR) << "read manifest file error"; return {}; }
    nlohmann::json manifest_json = yaml_to_json(*manifest_yaml);
    // 构造与旧 CRD 兼容的结构供 expand_ref 使用
    nlohmann::json compat;
    if (manifest_json.contains("schema") && manifest_json["schema"].contains("openAPIV3Schema")) {
        compat["spec"]["schema"]["openAPIV3Schema"] = manifest_json["schema"]["openAPIV3Schema"];
    }
    return compat;
}

// 展开 x-intentable
nlohmann::json expand_intentable(
    const nlohmann::json& j,
    nlohmann::json_pointer<std::string> current = nlohmann::json_pointer<std::string>{"/"}
) {
    if (j.is_object()) {
        if (j.contains("x-intentable")) {
            if (j.at("x-intentable").empty()) {
                throw std::invalid_argument("invalid intentable format at" + current.to_string());
            }
            // 展开为value和intent
            const auto& inner_schema = j.at("x-intentable");
            return {
                {"type", "object"},
                {"properties", {{"value", inner_schema}, {"intent", inner_schema}}},
                {"required", {"value"}}
            };
        }
        // 只是普通json,继续传递该函数
        nlohmann::json res;
        for (const auto& [key, value] : j.items()) {
            res[key] = expand_intentable(value, current / key);
        }
        return res;
    }
    else if (j.is_array()) {
        // 向下传递
        nlohmann::json res;
        for (const auto& [key, value] : j.items()) {
            auto key_num = stoi(key);
            res.push_back(expand_intentable(value, current / key_num));
        }
        return res;
    }
    // 是基础类型
    return j;
}

template <typename Integer>
Integer sv_to_integer(std::string_view sv) {
    Integer i3;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), i3);
    if (ec != std::error_code{}) { throw make_error("parse number failed:", sv); }
    return i3;
}
#define GUARD_OR_EXCEPTION(Condition) \
    if (!(Condition)) { throw std::invalid_argument("check failed:" #Condition); }

struct SubRefEntry {
    /// CR中Subability项的对应索引
    int number;
    /// 子能力CR status 中的对应路径
    std::string sub_key;
    static SubRefEntry parse(std::string_view ref) {
        //"/sub"在字符串中的尾后位置
        size_t l_pos = ref.find("/sub/") + 5;
        GUARD_OR_EXCEPTION(l_pos != ref.npos);
        size_t r_pos = ref.find('#');
        GUARD_OR_EXCEPTION(r_pos != ref.npos);
        auto number_str = ref.substr(l_pos, r_pos - l_pos);
        int number = sv_to_integer<int>(number_str);
        // 解析子能力status中哪一项,目前只支持status的直接子项
        auto last_slash_pos = ref.find_last_of('/');

        GUARD_OR_EXCEPTION(last_slash_pos != ref.npos);
        GUARD_OR_EXCEPTION(last_slash_pos + 1 != ref.npos);
        std::string_view sub_key = ref.substr(last_slash_pos + 1);
        return SubRefEntry{.number = number, .sub_key = std::string(sub_key)};
    }
};

// 展开 x-ref
nlohmann::json expand_ref(
    const nlohmann::json& j,
    const AbilityCR& cr,
    nlohmann::json_pointer<std::string> current = nlohmann::json_pointer<std::string>{"/"}
) {
    if (j.is_primitive()) {
        // 是基础类型
        return j;
    }
    if (j.is_array()) {
        // 向下传递
        nlohmann::json res;
        for (const auto& [key, value] : j.items()) {
            auto key_num = stoi(key);
            res.push_back(expand_intentable(value, current / key_num));
        }
        return res;
    }
    GUARD_OR_EXCEPTION(j.is_object());
    if (!j.contains("x-ref")) {
        // 只是普通json,继续传递该函数
        nlohmann::json res;
        for (const auto& [key, value] : j.items()) {
            res[key] = expand_ref(value, cr, current / key);
        }
        return res;
    }

    if (j.at("x-ref").empty()) {
        throw std::invalid_argument("invalid x-ref format at" + current.to_string());
    }
    // 展开为引用子能力的schema
    const std::string ref = j.at("x-ref").get<std::string>();
    // 解析 x-ref 哪个子能力
    SubRefEntry ref_entry = SubRefEntry::parse(ref);

    if (ref_entry.number >= cr.spec->subabilities.size()) {
        throw make_error<std::invalid_argument>(
            "x-ref out of range at ", current.to_string(), ", no such sub ability ",
            ref_entry.number
        );
    }
    // 获取引用子能力crd
    nlohmann::json crd_json = get_sub_ability_crd(cr, ref_entry.number);
    const nlohmann::json Empty;
    nlohmann::json crd_schema = crd_json.value("/spec/schema/openAPIV3Schema"_json_pointer, Empty);
    if (crd_schema.empty()) {
        throw std::invalid_argument("can't find /spec/schema/openAPIV3Schema");
    }
    // 展开子能力crd
    crd_schema = expand_intentable(crd_schema);
    scope_fail _complain([&]() {
        LOG(INFO) << "in sub " << ref_entry.number << " :\n" << crd_schema.dump(2);
    });
    nlohmann::json sub_status
        = crd_schema.value("/properties/status/properties"_json_pointer, Empty);
    if (!sub_status.contains(ref_entry.sub_key)) {
        scope_fail _complain([&]() {
            LOG(WARNING) << "in sub " << ref_entry.number << "'s crd_schema :\n"
                         << sub_status.dump(2);
        });
        throw make_error<std::invalid_argument>(
            "ref subability does ", ref_entry.number, " not contain sub_path: /status/",
            ref_entry.sub_key
        );
    }

    return sub_status[ref_entry.sub_key];
}

void make_status_in_crd_optional(nlohmann::json& j) {
    using namespace nlohmann;
    auto ptr = "/properties/status"_json_pointer;
    if (j.contains(ptr)) {
        auto status_subschema = j.at(ptr);
        json nullable_schema;
        nullable_schema["anyOf"].push_back({{"type", "null"}});
        nullable_schema["anyOf"].push_back(status_subschema);
        j[ptr] = nullable_schema;
    }
}

// 校验抽象能力cr (从 manifest 读取接口定义)
expected<void, std::string> check_abstract_ability_cr_validity(
    const AbilityCR& cr, const std::string& interface_crd
) {
    const nlohmann::json Empty;
    auto pkg_path = global_vars::home_path() / "packages"
                  / cr.spec->package / to_string(cr.spec->version);
    auto manifest_path = pkg_path / "ability.manifest.yaml";
    if (!exists(manifest_path)) { return {}; }
    auto manifest_yaml = read_yaml_from_path(manifest_path);
    if (!manifest_yaml) { LOG(ERROR) << "read manifest file error"; return unexpected{"read manifest error"}; }
    auto manifest_json = yaml_to_json(*manifest_yaml);
    nlohmann::json interface_crd_schema;
    if (manifest_json.contains("schema") && manifest_json["schema"].contains("openAPIV3Schema")) {
        interface_crd_schema = expand_intentable(manifest_json["schema"]["openAPIV3Schema"]);
        if (interface_crd_schema.empty()) {
            return unexpected{"can't find schema/openAPIV3Schema in manifest"};
        }
    }
    else { return {}; }
    for (int i = 0; i < cr.spec->subabilities.size(); ++i) {
        nlohmann::json sub_cr = *cr.spec->subabilities[i];
        auto res = validate_to_expected(sub_cr, interface_crd_schema);
        if (!res) { return unexpected{"inavlid cr: " + res.error()}; }
    }
    return {};
}

// 展开后校验
expected<void, ErrorMsg> validate_with_expanded_crd(
    const AbilityCR& cr, const nlohmann::json& crd_json
) try {
    using namespace nlohmann;
    const json Empty;
    json crd_schema = crd_json.value("/spec/schema/openAPIV3Schema"_json_pointer, Empty);
    if (crd_schema.empty()) { return unexpected{"can't find /spec/schema/openAPIV3Schema"}; }
    // 对schema进行展开，之后标准校验
    json expanded_crd;
    // x-intentable展开
    expanded_crd = expand_intentable(crd_schema);
    // x-ref展开
    expanded_crd = expand_ref(expanded_crd, cr);
    make_status_in_crd_optional(expanded_crd);
    json cr_json = cr;
    // 校验能力cr
    auto res = validate_to_expected(cr_json, expanded_crd);
    if (!res) { return unexpected{"inavlid cr: " + res.error()}; }
    // 如果是组合能力，继续校验其子能力
    if (cr.kind == "ComposeAbility") {
        for (int i = 0; i < cr.spec->subabilities.size(); ++i) {
            if (auto it = get_sub_ability_crd(cr, i); !it.empty()) {
                nlohmann::json sub_cr = *cr.spec->subabilities[i];
                json sub_crd_schema = expand_intentable(it).value(
                    "/spec/schema/openAPIV3Schema"_json_pointer, Empty
                );
                if (sub_crd_schema.empty()) {
                    return unexpected{"can't find /spec/schema/openAPIV3Schema"};
                }
                auto res = validate_to_expected(sub_cr, sub_crd_schema);
                if (!res) { return unexpected{"inavlid cr: " + res.error()}; }
            }
        }
    }
    if (cr.kind == "AbstractAbility") {
        json interface_crd
            = crd_json.value("/depends/subabilities/x-implement"_json_pointer, Empty);
        if (interface_crd.empty()) {
            return unexpected{"can't find /depends/subabilities/x-implement"};
        }
        if (!interface_crd.is_string()) {
            return unexpected{"/depends/subabilities/x-implement must be string"};
        }
        std::string interface_crd_str = interface_crd.get<std::string>();
        auto res = check_abstract_ability_cr_validity(cr, interface_crd_str);
        if (!res) { return unexpected{"inavlid cr: " + res.error()}; }
    }
    // 校验成功
    return {};
}
catch (std::exception& e) {
    return unexpected{e.what()};
}

// Phase 1: 框架级验证 - CR 整体结构符合内置 ability.crd schema
expected<void, ErrorMsg> validate_cr_framework_level(const nlohmann::json& cr_json) {
    const auto& schema = get_builtin_ability_crd_schema();
    return validate_to_expected(cr_json, schema);
}

// 强制检测: CR 必须声明 spec.tasks，每个 taskName 都需要在 manifest.tasks 中存在
expected<void, ErrorMsg> validate_cr_tasks_field(
    const nlohmann::json& cr_json, const AbilityManifest& manifest
) {
    using nlohmann::json;

    // 1. spec.tasks 必须存在
    if (!cr_json.contains("spec") || !cr_json["spec"].is_object()) {
        return unexpected{"missing required field: spec"};
    }
    const auto& spec = cr_json["spec"];
    if (!spec.contains("tasks")) {
        return unexpected{
            "missing required field: spec.tasks (CR must declare which manifest tasks it uses)"
        };
    }
    const auto& tasks = spec["tasks"];
    if (!tasks.is_array() || tasks.empty()) {
        return unexpected{"spec.tasks must be a non-empty array"};
    }

    // 2. 收集 manifest 中的合法 taskName
    std::unordered_set<std::string> manifest_task_names;
    if (manifest.tasks.is_array()) {
        for (const auto& mt : manifest.tasks) {
            if (mt.is_object() && mt.contains("taskName") && mt["taskName"].is_string()) {
                manifest_task_names.insert(mt["taskName"].get<std::string>());
            }
        }
    }
    if (manifest_task_names.empty()) {
        return unexpected{"manifest defines no tasks; cannot validate CR tasks"};
    }

    // 3. 校验每个 task 项
    std::string errors;
    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& t = tasks[i];
        if (!t.is_object()) {
            errors += "\n  - spec.tasks[" + std::to_string(i) + "] must be an object";
            continue;
        }
        if (!t.contains("taskName") || !t["taskName"].is_string()) {
            errors += "\n  - spec.tasks[" + std::to_string(i)
                    + "] missing required field: taskName";
            continue;
        }
        const std::string name = t["taskName"].get<std::string>();
        if (manifest_task_names.find(name) == manifest_task_names.end()) {
            errors += "\n  - spec.tasks[" + std::to_string(i) + "] taskName '" + name
                    + "' not found in manifest tasks";
        }
    }
    if (!errors.empty()) { return unexpected{"task field validation failed:" + errors}; }
    return {};
}

// Phase 1: 能力级验证 - CR 的 config/debugOption/status 符合 manifest schema
expected<void, ErrorMsg> validate_cr_manifest_level(
    const AbilityCR& cr, const AbilityManifest& manifest
) try {
    using namespace nlohmann;
    const json Empty;

    // 如果 manifest 没有 openAPIV3Schema, 跳过能力级验证
    if (manifest.schema.openAPIV3Schema.is_null()
        || manifest.schema.openAPIV3Schema.empty()) {
        return {};
    }

    json crd_schema = manifest.schema.openAPIV3Schema;
    // x-intentable 展开
    json expanded = expand_intentable(crd_schema);
    // x-ref 展开
    expanded = expand_ref(expanded, cr);
    make_status_in_crd_optional(expanded);

    json cr_json = cr;
    auto res = validate_to_expected(cr_json, expanded);
    if (!res) { return unexpected{"manifest-level validation failed: " + res.error()}; }
    return {};
}
catch (std::exception& e) {
    return unexpected{std::string("manifest-level validation error: ") + e.what()};
}
