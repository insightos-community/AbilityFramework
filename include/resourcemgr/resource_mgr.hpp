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
#include "ability_manifest.hpp"
#include "ability_pkg.hpp"
#include "cr_crd_common.hpp"
#include "service_cr.hpp"
#include "messagebus/basic_module.hpp"
#include "param/ability_class_info.hpp"
#include "param/ability_storage_info.hpp"
#include "resourcemgr/ability_cr.hpp"
#include "resourcemgr/ability_crd.hpp"
#include "resourcemgr/device_cr.hpp"
#include "resourcemgr/device_crd.hpp"
#include "resourcemgr/model_mgr.hpp"
#include "resourcemgr/store_mgr.hpp"
#include "taskmgr/task.hpp"
#include "util/expected.hpp"
#include "util/generator.hpp"
#include "util/global_vars.hpp"
#include "util/make_uuid.hpp"
#include "util/yaml_to_json.hpp"
#include <httplib.h>
#include <mutex>
#include <uuid.h>

class ResourceManager;
/// @brief 检查某个能力是否有父能力
/// @return 如果是,则返回父能力的id,其他所有情况都返回nullopt
std::optional<uuids::uuid> find_cr_parent(ResourceManager& mgr, uuids::uuid instance_id);

class ResourceManager : public message_bus::BasicModule<ResourceManager> {
    using Super = message_bus::BasicModule<ResourceManager>;
    using ID = uuids::uuid;
    using IDString = std::string;
    mutable std::recursive_mutex m;
    // 搜索到的所有包,暂时以数组的形式存储
    // 以后可能需要其他索引,比如能力名,版本等
    // 届时就将哪个带索引的结构体单独作为一个容器S
    std::vector<AbilityPackage> packages;

    // 框架读取cr时将组合能力和抽象能力存储其中，后续为其添加子能力cr和子能力项，避免再从redis中读取判断组合和抽象能力
    std::vector<AbilityCR> compose_abstract_abilities;

    //  框架的根路径
    std::filesystem::path framework_home;
    // 框架id
    uuids::uuid framework_id;
    StoreManager store_mgr;
    std::shared_ptr<ModelManager> model_mgr;

    // autoStart
    struct StartRecord {
        std::chrono::steady_clock::time_point last_start;
        int retry_count = 0;
    };
    std::unordered_map<uuids::uuid, StartRecord> start_records;

public:
    ResourceManager();
    void on_exit() override { clear(); }

    [[nodiscard]] std::string module_name() const override;
    void add_message_handlers() override {
        ON_QUERY("HeartbeatEvent", on_heartbeat_event);
        ON_QUERY("find_ability_storage_info/id", find_ability_storage_info);
        ON_QUERY("find_ability_instance/id", on_query_find_ability_instance);
        ON_QUERY("get_ability", on_query_find_ability_id);
        ON_QUERY("find_parent/id", on_query_find_parent);
        ON_QUERY("ability_instance_exited", on_ability_instance_exited);
    };

    void on_register() override;

    // 更新自己的各项资源
    void update();

    // 框架退出时从redis中移除由本框架添加的cr
    void clear();

    // 获取框架id
    uuids::uuid get_framework_id() const { return framework_id; }

    // 获取能力共享者，id, position
    std::unordered_map<uuids::uuid, std::string> get_ability_sharers(
        const uuids::uuid& abilityInstance_id
    );
    // 获取设备共享者，id, position
    std::unordered_map<uuids::uuid, std::string> get_device_sharers(
        const uuids::uuid& deviceInstance_id
    );
    struct OwnerInfo {
        uuids::uuid id;
        std::string position;
    };
    // 获取能力占用者，id, position
    std::optional<OwnerInfo> get_ability_owner(const uuids::uuid& abilityInstance_id);
    // 获取设备占用者，id, position
    std::optional<OwnerInfo> get_device_owner(const uuids::uuid& deviceInstance_id);
    // 添加能力共享者
    bool add_ability_sharers(
        const ID& parent_id, const ID& abilityInstance_id, const std::string& position
    );
    // 添加设备共享者
    bool add_device_sharers(
        const ID& parent_id, const ID& deviceInstance_id, const std::string& position
    );
    // 添加能力独占者
    bool add_ability_owner(
        const ID& parent_id, const ID& abilityInstance_id, const std::string& position
    );
    // 添加设备独占者
    bool add_device_owner(
        const ID& parent_id, const ID& deviceInstance_id, const std::string& position
    );
    // 删除能力共享者
    bool remove_ability_sharer(const ID& parent_id, const uuids::uuid& abilityInstance_id);
    // 删除设备共享者
    bool remove_device_sharer(const ID& parent_id, const uuids::uuid& deviceInstance_id);
    // 删除能力独占者
    bool remove_ability_owner(const ID& parent_id, const uuids::uuid& abilityInstance_id);
    // 删除设备独占者
    bool remove_device_owner(const ID& parent_id, const uuids::uuid& deviceInstance_id);
    // 如果失败,返回错误原因
    expected<void, std::string> occupy_ability(
        uuids::uuid occupy_ability_id,
        std::string position,
        uuids::uuid occupied_ability_id,
        OwnershipMode mode
    );
    // 如果失败,返回错误原因
    expected<void, std::string> occupy_device(
        uuids::uuid occupy_device_id,
        std::string position,
        uuids::uuid occupied_device_id,
        OwnershipMode mode
    );
    // bool 表示是否执行了unoccupy的过程
    // 如果这一设备不在本地,那么找到对应的远程主机,向其发送(unoccupy_ability)的操作
    bool unoccupy_ability(
        uuids::uuid occupy_ability_id, uuids::uuid occupied_ability_id, OwnershipMode mode
    );
    // bool 表示是否执行了unoccupy的过程
    bool unoccupy_device(
        uuids::uuid occupy_device_id, uuids::uuid occupied_device_id, OwnershipMode mode
    );

    /// @brief 添加cr
    /// @returns 字符串格式的能力实例 id
    [[nodiscard]]
    IDString add_ability_cr(const AbilityCR& cr);
    void add_device_cr(const DeviceCR& cr);
    // 删除cr
    void remove_ability_cr(ID id);
    void remove_device_cr(ID id);

    // 添加crd
    expected<void, std::string> add_ability_crd(const AbilityCRD& crd);
    expected<void, std::string> add_device_crd(const DeviceCRD& crd);
    // 删除crd
    void remove_ability_crd(ID id);
    void remove_device_crd(ID id);
    // 获取能力crd结构体
    std::optional<AbilityCRD> get_ability_crd(
        const std::string& ability_name, const std::string& version
    ) const;

    std::optional<AbilityCRD> get_ability_crd_by_name(std::string_view crd_name) const;
    // 获取设备crd结构体
    std::optional<DeviceCRD> get_device_crd(
        const std::string& device_name, const std::string& device_version
    ) const;
    std::optional<DeviceCRD> get_device_crd_by_name(std::string_view crd_name) const;

    // 根据能力实例判断能力是否存在
    bool judge_ability_exist(const uuids::uuid& abilityInstance_id) const;
    // 根据设备实例判断设备是否存在
    bool judge_device_exist(const uuids::uuid& deviceInstance_id) const;
    // 获取能力cr结构体 (仅查模板表 AbilityCRBasic)
    std::optional<AbilityCR> get_ability_cr(ID abilityInstance_id) const;

    // ======== 运行时实例 ========
    // 实例元信息
    struct AbilityInstanceInfo {
        uuids::uuid instance_id;
        std::optional<uuids::uuid> cr_id;
        std::string cr_name;
        std::string instance_name;
        std::string ability_name;
        std::string ability_version;
        std::string state;
        int64_t start_time = 0;
        int64_t stop_time = 0;
        nlohmann::json detail;
        nlohmann::json spec_snapshot;
    };
    // 从模板创建一个运行时实例 (产生新的 instance_id)；返回新实例 id
    [[nodiscard]] uuids::uuid create_ability_instance(const AbilityCR& cr_template);
    // 删除实例行 (实例退出或 terminate 时调用)
    void delete_ability_instance(ID instance_id);
    // 查询单个实例
    std::optional<AbilityInstanceInfo> get_ability_instance(ID instance_id) const;
    // 所有实例
    std::vector<AbilityInstanceInfo> get_all_ability_instances() const;
    // 按 ability_name 查活跃实例（用于 singleton 检查）
    std::vector<AbilityInstanceInfo> get_active_instances_by_ability_name(
        const std::string& ability_name
    ) const;
    // 解析 instance_id 对应的运行时 CR（从 spec_snapshot 还原，失败时回退到模板表）
    std::optional<AbilityCR> resolve_ability_cr_by_id(ID id) const;

    expected<AbilityCR, ErrorMsg> on_query_find_ability_instance(ID id) const {
        auto op = resolve_ability_cr_by_id(id);
        if (op) { return std::move(*op); }
        return unexpected{"ability: " + to_string(id) + " not found"};
    }
    // LifecycleMgr 通知: 子进程退出，应销毁实例行
    expected<nlohmann::json, std::string> on_ability_instance_exited(uuids::uuid instance_id) {
        delete_ability_instance(instance_id);
        return nlohmann::json{{"status", "ok"}};
    }
    // 获取设备cr结构体
    std::optional<DeviceCR> get_device_cr(ID deviceInstance_id);
    // 获取redis中所有能力cr
    std::vector<AbilityCR> get_all_ability_cr();
    // 获取由本框架添加的能力cr
    std::vector<AbilityCR> get_local_ability_crs();
    // 获取redis中所有设备cr
    std::vector<DeviceCR> get_all_device_cr();
    // 获取本框架添加的设备cr
    std::vector<DeviceCR> get_local_device_crs();

    std::vector<AbilityCRD> get_all_ability_crd() const;
    std::vector<DeviceCRD> get_all_device_crd() const;

    // Phase 1: 根据能力名和版本获取 manifest
    std::optional<AbilityManifest> get_ability_manifest(
        const std::string& ability_name, const std::string& version
    ) const;

    // 根据能力类名查找运行中的实例 ID 列表（用于单例检查）
    // 查询的是 AbilityInstance 表中 state != Terminated/Inactive 的行
    std::vector<std::string> get_heartbeats_by_ability_name(const std::string& ability_name) const;

    // Phase 2: Service CR 管理
    std::string add_service_cr(const ServiceCR& cr);
    void remove_service_cr(uuids::uuid id);
    std::optional<ServiceCR> get_service_cr(uuids::uuid id) const;
    std::vector<ServiceCR> get_all_service_cr() const;

    // 根据能力实例id查找能力类id
    std::optional<msg_params::AbilityClassInfo> find_ability_id(ID abilityInstance_id);
    expected<msg_params::AbilityClassInfo, ErrorMsg> on_query_find_ability_id(ID instance_id) {
        if (auto it = find_ability_id(instance_id); it) { return std::move(*it); }
        return unexpected{"ability: " + to_string(instance_id) + " not found"};
    }
    void show() const;

    // 根据cr中的配置，启动能力
    void auto_start_ability();

    // 给定CR,检查它是否是合法的,即是否能构造出能力
    // 这包括递归地检查子能力,它们是否合法
    // 以及检查cr
    [[nodiscard]] expected<void, std::string> check_ability_cr_validity(const AbilityCR& cr) const;
    [[nodiscard]] expected<void, std::string> check_device_cr_validity(const DeviceCR& cr) const;

    expected<uuids::uuid, ErrorMsg> on_query_find_parent(ID id) {
        auto op = find_cr_parent(*this, id);
        if (op) { return std::move(*op); }
        return unexpected{"ability: " + to_string(id) + " not found"};
    }

    friend class ResourceMgrModule;
    friend void build_api(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);
    // build_api 的子函数
    friend void build_api_cr(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);
    friend void build_api_occupation(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);
    // build_api 的子函数
    friend void build_api_crd(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);

    TaskPtr task_download_package(PackageSpec pkg_spec);

private:
    expected<msg_params::AbilityStorageInfo, std::string> find_ability_storage_info(
        uuids::uuid instance_id
    );

    // mutable RedisHelper redis;
    // 读取框架内的cr文件
    expected<void, std::string> read_cr_in_framework_cr_dir(const std::filesystem::path& path_crs);

public:
    // 心跳事件入口（消息总线 + 单元测试 共用）
    //
    // 返回值:
    //   true  — 心跳被接受 (instance_id 命中 AbilityInstance 表, 状态已更新)
    //   false — 孤儿心跳 (instance_id 未知, 被丢弃, 调用方应回 410 Gone 让
    //           发送方自毁; 这是清理上一代 framework 留下的 zombie python
    //           进程的关键信号)
    [[nodiscard]] bool on_event_heartbeat(const Heartbeat&);
    expected<nlohmann::json, std::string> on_heartbeat_event(Heartbeat hb);
private:
    expected<void, std::string> check_local_ability_cr_validity(const AbilityCR& cr) const;
    expected<void, std::string> check_local_device_cr_validity(const DeviceCR& cr) const;

    void read_crs_into_db(const std::filesystem::path& path_crs);
    // 为组合能力和抽象能力添加子能力项和子能力cr
    expected<void, std::string> add_subabilities();
};

bool is_ability_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id);
bool is_device_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id);
bool is_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id);
