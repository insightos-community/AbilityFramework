// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
/// @brief check whether an ability has a parent ability
/// @return if it is,then return the parent ability'sid,otherallcases allreturnnullopt
std::optional<uuids::uuid> find_cr_parent(ResourceManager& mgr, uuids::uuid instance_id);

class ResourceManager : public message_bus::BasicModule<ResourceManager> {
    using Super = message_bus::BasicModule<ResourceManager>;
    using ID = uuids::uuid;
    using IDString = std::string;
    mutable std::recursive_mutex m;
    // all searched packages, temporarily stored as an array
    // may need other indices in the future,e.g.ability name,version, etc.
    // thenthen takes the indexed struct as a standalone container
    std::vector<AbilityPackage> packages;

    // when reading CRs the framework stores composite and abstract abilities here, then adds sub-ability CRs and items, avoiding re-reading from redis to determine composite/abstract
    std::vector<AbilityCR> compose_abstract_abilities;

    // root path of the framework
    std::filesystem::path framework_home;
    // framework id
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

    // update own resources
    void update();

    // on framework exit, crs added by this framework are removed from redis
    void clear();

    // getframework id
    uuids::uuid get_framework_id() const { return framework_id; }

    // get ability sharers, id, position
    std::unordered_map<uuids::uuid, std::string> get_ability_sharers(
        const uuids::uuid& abilityInstance_id
    );
    // get device sharers, id, position
    std::unordered_map<uuids::uuid, std::string> get_device_sharers(
        const uuids::uuid& deviceInstance_id
    );
    struct OwnerInfo {
        uuids::uuid id;
        std::string position;
    };
    // get ability occupiers, id, position
    std::optional<OwnerInfo> get_ability_owner(const uuids::uuid& abilityInstance_id);
    // get device occupiers, id, position
    std::optional<OwnerInfo> get_device_owner(const uuids::uuid& deviceInstance_id);
    // add ability sharer
    bool add_ability_sharers(
        const ID& parent_id, const ID& abilityInstance_id, const std::string& position
    );
    // add device sharer
    bool add_device_sharers(
        const ID& parent_id, const ID& deviceInstance_id, const std::string& position
    );
    // add ability occupier
    bool add_ability_owner(
        const ID& parent_id, const ID& abilityInstance_id, const std::string& position
    );
    // add device occupier
    bool add_device_owner(
        const ID& parent_id, const ID& deviceInstance_id, const std::string& position
    );
    // remove ability sharer
    bool remove_ability_sharer(const ID& parent_id, const uuids::uuid& abilityInstance_id);
    // remove device sharer
    bool remove_device_sharer(const ID& parent_id, const uuids::uuid& deviceInstance_id);
    // remove ability occupier
    bool remove_ability_owner(const ID& parent_id, const uuids::uuid& abilityInstance_id);
    // remove device occupier
    bool remove_device_owner(const ID& parent_id, const uuids::uuid& deviceInstance_id);
    // on failure, return error reason
    expected<void, std::string> occupy_ability(
        uuids::uuid occupy_ability_id,
        std::string position,
        uuids::uuid occupied_ability_id,
        OwnershipMode mode
    );
    // on failure, return error reason
    expected<void, std::string> occupy_device(
        uuids::uuid occupy_device_id,
        std::string position,
        uuids::uuid occupied_device_id,
        OwnershipMode mode
    );
    // bool indicates whether executed the unoccupy process
    // if this device is not local, find the corresponding remote host and send the (unoccupy_ability) operation to it
    bool unoccupy_ability(
        uuids::uuid occupy_ability_id, uuids::uuid occupied_ability_id, OwnershipMode mode
    );
    // bool indicates whether executed the unoccupy process
    bool unoccupy_device(
        uuids::uuid occupy_device_id, uuids::uuid occupied_device_id, OwnershipMode mode
    );

    /// @brief add cr
    /// @returns string formatability instance id
    [[nodiscard]]
    IDString add_ability_cr(const AbilityCR& cr);
    void add_device_cr(const DeviceCR& cr);
    // delete cr
    void remove_ability_cr(ID id);
    void remove_device_cr(ID id);

    // add crd
    expected<void, std::string> add_ability_crd(const AbilityCRD& crd);
    expected<void, std::string> add_device_crd(const DeviceCRD& crd);
    // delete crd
    void remove_ability_crd(ID id);
    void remove_device_crd(ID id);
    // get ability crd struct
    std::optional<AbilityCRD> get_ability_crd(
        const std::string& ability_name, const std::string& version
    ) const;

    std::optional<AbilityCRD> get_ability_crd_by_name(std::string_view crd_name) const;
    // get device crd struct
    std::optional<DeviceCRD> get_device_crd(
        const std::string& device_name, const std::string& device_version
    ) const;
    std::optional<DeviceCRD> get_device_crd_by_name(std::string_view crd_name) const;

    // determine whether an ability exists by ability instance
    bool judge_ability_exist(const uuids::uuid& abilityInstance_id) const;
    // check whether the device exists by device instance
    bool judge_device_exist(const uuids::uuid& deviceInstance_id) const;
    // get ability cr struct (query template table AbilityCRBasic only)
    std::optional<AbilityCR> get_ability_cr(ID abilityInstance_id) const;

    // ======== runtime instance ========
    // instance metadata
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
    // create a runtime instance from a template (generate a new instance_id);return new instance id
    [[nodiscard]] uuids::uuid create_ability_instance(const AbilityCR& cr_template);
    // delete instance row (called when the instance exits or terminates)
    void delete_ability_instance(ID instance_id);
    // query a single instance
    std::optional<AbilityInstanceInfo> get_ability_instance(ID instance_id) const;
    // allinstance
    std::vector<AbilityInstanceInfo> get_all_ability_instances() const;
    // find active instances by ability_name (for singleton check)
    std::vector<AbilityInstanceInfo> get_active_instances_by_ability_name(
        const std::string& ability_name
    ) const;
    // parse the runtime CR for the given instance_id (restore from spec_snapshot; fall back to template table on failure)
    std::optional<AbilityCR> resolve_ability_cr_by_id(ID id) const;

    expected<AbilityCR, ErrorMsg> on_query_find_ability_instance(ID id) const {
        auto op = resolve_ability_cr_by_id(id);
        if (op) { return std::move(*op); }
        return unexpected{"ability: " + to_string(id) + " not found"};
    }
    // LifecycleMgr notification: subprocess exited, should destroy the instance row
    expected<nlohmann::json, std::string> on_ability_instance_exited(uuids::uuid instance_id) {
        delete_ability_instance(instance_id);
        return nlohmann::json{{"status", "ok"}};
    }
    // get device cr struct
    std::optional<DeviceCR> get_device_cr(ID deviceInstance_id);
    // get all ability crs in redis
    std::vector<AbilityCR> get_all_ability_cr();
    // get ability crs added by this framework
    std::vector<AbilityCR> get_local_ability_crs();
    // get all device crs in redis
    std::vector<DeviceCR> get_all_device_cr();
    // get device crs added by this framework
    std::vector<DeviceCR> get_local_device_crs();

    std::vector<AbilityCRD> get_all_ability_crd() const;
    std::vector<DeviceCRD> get_all_device_crd() const;

    // Phase 1: get manifest by ability name and version
    std::optional<AbilityManifest> get_ability_manifest(
        const std::string& ability_name, const std::string& version
    ) const;

    // find running instance ID list by ability class name (for singleton check)
    // queries rows where state != Terminated/Inactive in the AbilityInstance table
    std::vector<std::string> get_heartbeats_by_ability_name(const std::string& ability_name) const;

    // Phase 2: Service CR management
    std::string add_service_cr(const ServiceCR& cr);
    void remove_service_cr(uuids::uuid id);
    std::optional<ServiceCR> get_service_cr(uuids::uuid id) const;
    std::vector<ServiceCR> get_all_service_cr() const;

    // find ability class id by ability instance id
    std::optional<msg_params::AbilityClassInfo> find_ability_id(ID abilityInstance_id);
    expected<msg_params::AbilityClassInfo, ErrorMsg> on_query_find_ability_id(ID instance_id) {
        if (auto it = find_ability_id(instance_id); it) { return std::move(*it); }
        return unexpected{"ability: " + to_string(instance_id) + " not found"};
    }
    void show() const;

    // start the ability according to config in the CR
    void auto_start_ability();

    // given a CR, check whether it is valid, i.e. whether an ability can be constructed
    // this includes recursively checking sub-abilities for validity
    // as well ascheckcr
    [[nodiscard]] expected<void, std::string> check_ability_cr_validity(const AbilityCR& cr) const;
    [[nodiscard]] expected<void, std::string> check_device_cr_validity(const DeviceCR& cr) const;

    expected<uuids::uuid, ErrorMsg> on_query_find_parent(ID id) {
        auto op = find_cr_parent(*this, id);
        if (op) { return std::move(*op); }
        return unexpected{"ability: " + to_string(id) + " not found"};
    }

    friend class ResourceMgrModule;
    friend void build_api(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);
    // build_api subfunction
    friend void build_api_cr(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);
    friend void build_api_occupation(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);
    // build_api subfunction
    friend void build_api_crd(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);

    TaskPtr task_download_package(PackageSpec pkg_spec);

private:
    expected<msg_params::AbilityStorageInfo, std::string> find_ability_storage_info(
        uuids::uuid instance_id
    );

    // mutable RedisHelper redis;
    // read CR files within the framework
    expected<void, std::string> read_cr_in_framework_cr_dir(const std::filesystem::path& path_crs);

public:
    // heartbeat event entry (shared by message bus + unit tests)
    //
    // return value:
    // true — heartbeat accepted (instance_id hits AbilityInstance table, status updated)
    // false — orphan heartbeat (instance_id unknown, discarded; caller should return 410 Gone so
    // sender self-destructs; this cleans up zombie python left by the previous framework
    // key signal of the process)
    [[nodiscard]] bool on_event_heartbeat(const Heartbeat&);
    expected<nlohmann::json, std::string> on_heartbeat_event(Heartbeat hb);
private:
    expected<void, std::string> check_local_ability_cr_validity(const AbilityCR& cr) const;
    expected<void, std::string> check_local_device_cr_validity(const DeviceCR& cr) const;

    void read_crs_into_db(const std::filesystem::path& path_crs);
    // add sub-ability items and sub-ability crs for composite and abstract abilities
    expected<void, std::string> add_subabilities();
};

bool is_ability_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id);
bool is_device_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id);
bool is_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id);
