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

#pragma once
#include "cr_crd_common.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"
#include <nlohmann/json.hpp>
#include <string>
struct AbilityCRD {
    uuids::uuid id;
    std::string packageName; // 该种能力位于哪个包
    semver::version version;
    std::string kind;
    struct Metadata {
        std::string name;
        std::unordered_map<std::string, std::string> labels;
        std::unordered_map<std::string, std::string> annotations;

        friend void from_json(const nlohmann::json&, Metadata&);
        friend void to_json(nlohmann::json&, const Metadata&);
    };
    Metadata metadata;
    struct AbilityDep {
        std::string package;
        std::string abilityName; // 能力类型名
        OwnershipMode ownership{OwnershipMode::unique};
        BindMode bind{BindMode::local};
        std::string description; // 可选的描述文本
        friend void from_json(const nlohmann::json&, AbilityDep&);
        friend void to_json(nlohmann::json&, const AbilityDep&);
    };
    struct DeviceDep {
        std::string package;
        std::string deviceName; // 设备类型名
        OwnershipMode ownership{OwnershipMode::unique};
        BindMode bind{BindMode::local};
        std::string description; // 可选的描述文本
        friend void from_json(const nlohmann::json&, DeviceDep&);
        friend void to_json(nlohmann::json&, const DeviceDep&);
    };
    struct Deps {
        std::vector<AbilityDep> abilities;           // 子能力依赖
        std::vector<AbilityDep> additionalAbilities; // 附加子能力依赖
        // 如果depends/abilities不是数组,而只有一项,
        // 表示该能力接受不定数量的子能力,但要求是统一的
        // 常用于替换集中的能力
        // 如果它为真,则abilities中只有唯一的一项
        bool num_ability_is_arbitrary = false;
        std::vector<DeviceDep> devices;           // 设备依赖
        std::vector<DeviceDep> additionalDevices; // 附加子能力依赖
        friend void from_json(const nlohmann::json&, DeviceDep&);
        friend void to_json(nlohmann::json&, const DeviceDep&);
        [[nodiscard]] bool empty() const {
            return abilities.empty() && devices.empty() && additionalDevices.empty()
                && additionalAbilities.empty();
        }
    };
    Deps depends;
    struct SchemaInfo {
        nlohmann::json constants;       // 为上级能力提供常量
        nlohmann::json openAPIV3Schema; // 自己的Schema
        friend void from_json(const nlohmann::json&, SchemaInfo&);
        friend void to_json(nlohmann::json&, const SchemaInfo&);
    };
    struct Spec {
        nlohmann::json provides;
        SchemaInfo schema;
        nlohmann::json types;
        nlohmann::json rpcMethods;
        nlohmann::json tasks;
        nlohmann::json config;
        nlohmann::json debugOption;
        friend void from_json(const nlohmann::json&, Spec&);
        friend void to_json(nlohmann::json&, const Spec&);
    };
    Spec spec;
    // 框架处理CRD时用的信息,并不对外暴露
    struct FwkDetail {
        // 这个CRD是否计算完成,即它的depends项已经处理完毕,
        // ref,shadow,default 是否完全展开
        // ResourceMgr 会保留一份未展开的原件,
        // 但是校验时,需要用到已展开的CRD
        bool complete = false;
        // /spec/schema/openAPIV3Schema 中的x-ref 项是否展开
        bool ref_expanded = false;
        // 根据需要,这里可能有更多的内容
        friend void from_json(const nlohmann::json&, FwkDetail&);
        friend void to_json(nlohmann::json&, const FwkDetail&);
    };
    FwkDetail fwk;

    friend void from_json(const nlohmann::json&, AbilityCRD&);
    friend void to_json(nlohmann::json&, const AbilityCRD&);
};
