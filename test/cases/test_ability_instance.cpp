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

// 验证 CR (模板) 与 Instance (运行时) 严格分离的单元测试
//   - 启动一个实例不会改 CR 行
//   - 实例 id 与 CR id 不同
//   - 同模板可以派生多个实例 (singleton=false)
//   - delete 后实例从表中消失
//   - heartbeat 进入 Terminated 时实例自动销毁
//   - 单例约束：activeInstancesByAbilityName 反映运行中实例
#include "doctest.h"

#include "databasemgr/database_mgr.hpp"
#include "lifecyclemgr/heartbeat.hpp"
#include "resourcemgr/ability_cr.hpp"
#include "resourcemgr/resource_mgr.hpp"
#include "resourcemgr/store_mgr.hpp"
#include "util/global_vars.hpp"
#include "util/yaml_to_json.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

namespace fs = std::filesystem;

constexpr char ABILITY_CR_TEMPLATE[] = R"yaml(
kind: AtomAbility
metadata:
  name: dummy-ability
spec:
  package: dummy.pkg
  version: 1.0.0
  abilityName: DummyAbility
  position: localhost
  config: {}
  autoStart: false
  singleton: false
)yaml";

constexpr char ABILITY_CR_TEMPLATE_SINGLETON[] = R"yaml(
kind: AtomAbility
metadata:
  name: solo-ability
spec:
  package: solo.pkg
  version: 1.0.0
  abilityName: SoloAbility
  position: localhost
  config: {}
  autoStart: false
  singleton: true
)yaml";

// 每个测试套件用独立的 framework 主目录 + 内存数据库, 避免互相干扰。
struct ScopedTestEnv {
    fs::path home;
    bool owns_db = false;

    ScopedTestEnv() {
        std::random_device rd;
        std::mt19937 gen(rd());
        auto unique = std::to_string(gen());
        home = fs::temp_directory_path() / ("af_test_" + unique);
        fs::create_directories(home / "databases");
        fs::create_directories(home / "log");
        fs::create_directories(home / "packages");
        fs::create_directories(home / "crs");

        // 最小 config.yaml: 只设置 framework_name, 其余走默认
        std::ofstream cfg(home / "config.yaml");
        cfg << "framework_name: test_fwk_" << unique << "\n";
        cfg.close();

#ifdef _WIN32
        ::_putenv_s("ABILITY_FRAMEWORK_HOME", home.string().c_str());
#else
        ::setenv("ABILITY_FRAMEWORK_HOME", home.c_str(), 1);
#endif
        // global_vars::init 是幂等更新, 多次调用是安全的
        global_vars::init();

        try {
            database_mgr::init_database(home / "databases" / "test.db");
            owns_db = true;
        } catch (const std::exception&) {
            // 同一进程内多个测试可能复用全局 DB; 第二次 init_database
            // 会抛, 直接复用即可
            owns_db = false;
        }
    }

    ~ScopedTestEnv() {
        if (owns_db) {
            try { database_mgr::close_database(); } catch (...) {}
        }
        std::error_code ec;
        fs::remove_all(home, ec);
    }
};

AbilityCR make_template(const char* yaml) {
    auto y = YAML::Load(yaml);
    auto j = yaml_to_json(y);
    return j.get<AbilityCR>();
}

} // namespace

TEST_CASE("instance id differs from template (CR) id") {
    ScopedTestEnv env;
    ResourceManager mgr;

    auto cr = make_template(ABILITY_CR_TEMPLATE);
    auto cr_id_str = mgr.add_ability_cr(cr);
    auto cr_id = uuids::uuid::from_string(cr_id_str).value();
    auto template_opt = mgr.get_ability_cr(cr_id);
    REQUIRE(template_opt.has_value());

    auto instance_id = mgr.create_ability_instance(*template_opt);
    CHECK(instance_id != cr_id);
    CHECK(instance_id != uuids::uuid{});

    // 实例行存在, CR 行也仍在
    auto info = mgr.get_ability_instance(instance_id);
    REQUIRE(info.has_value());
    CHECK(info->cr_id.has_value());
    CHECK(*info->cr_id == cr_id);
    CHECK(info->ability_name == "DummyAbility");
    CHECK(info->state == "Inactive");

    // 模板没被实例创建动作改写
    auto template_after = mgr.get_ability_cr(cr_id);
    REQUIRE(template_after.has_value());
    CHECK(template_after->metadata.name == "dummy-ability");
}

TEST_CASE("multiple instances from one non-singleton template have unique ids") {
    ScopedTestEnv env;
    ResourceManager mgr;

    auto cr = make_template(ABILITY_CR_TEMPLATE);
    (void)mgr.add_ability_cr(cr);
    auto template_opt = mgr.get_ability_cr(
        uuids::uuid::from_string(mgr.add_ability_cr(cr)).value()
    );
    REQUIRE(template_opt.has_value());

    auto i1 = mgr.create_ability_instance(*template_opt);
    auto i2 = mgr.create_ability_instance(*template_opt);
    auto i3 = mgr.create_ability_instance(*template_opt);
    CHECK(i1 != i2);
    CHECK(i2 != i3);
    CHECK(i1 != i3);

    auto active = mgr.get_active_instances_by_ability_name("DummyAbility");
    // 状态默认 Inactive, 所以 active 列表此时为空
    CHECK(active.empty());

    auto all = mgr.get_all_ability_instances();
    CHECK(all.size() >= 3);
}

TEST_CASE("delete_ability_instance removes the row") {
    ScopedTestEnv env;
    ResourceManager mgr;

    auto cr = make_template(ABILITY_CR_TEMPLATE);
    auto cr_id_str = mgr.add_ability_cr(cr);
    auto template_opt = mgr.get_ability_cr(uuids::uuid::from_string(cr_id_str).value());
    REQUIRE(template_opt.has_value());

    auto instance_id = mgr.create_ability_instance(*template_opt);
    REQUIRE(mgr.get_ability_instance(instance_id).has_value());

    mgr.delete_ability_instance(instance_id);
    CHECK(!mgr.get_ability_instance(instance_id).has_value());
}

TEST_CASE("Terminated heartbeat destroys the instance row") {
    ScopedTestEnv env;
    ResourceManager mgr;

    auto cr = make_template(ABILITY_CR_TEMPLATE);
    auto cr_id_str = mgr.add_ability_cr(cr);
    auto template_opt = mgr.get_ability_cr(uuids::uuid::from_string(cr_id_str).value());
    REQUIRE(template_opt.has_value());

    auto instance_id = mgr.create_ability_instance(*template_opt);
    REQUIRE(mgr.get_ability_instance(instance_id).has_value());

    Heartbeat hb;
    hb.id = instance_id;
    hb.abilityName = "DummyAbility";
    hb.version = "1.0.0";
    hb.instanceName = "dummy";
    hb.state = LifecycleState::Running;
    hb.IPCPort = 0;
    hb.abilityPort = 0;
    CHECK(mgr.on_event_heartbeat(hb));  // accepted — instance is known

    auto running = mgr.get_ability_instance(instance_id);
    REQUIRE(running.has_value());
    CHECK(running->state == "Running");

    auto active = mgr.get_active_instances_by_ability_name("DummyAbility");
    CHECK(active.size() == 1);

    hb.state = LifecycleState::Terminated;
    CHECK(mgr.on_event_heartbeat(hb));  // Terminated is always accepted
    CHECK(!mgr.get_ability_instance(instance_id).has_value());
    CHECK(mgr.get_active_instances_by_ability_name("DummyAbility").empty());
}

TEST_CASE("orphan heartbeat is dropped and not stored") {
    // Regression: a framework restart leaves stale python ability processes
    // hitting /api/ability-heartbeat. Previously the fallback INSERT branch
    // created "ghost" rows with empty cr_id / spec_snapshot — making the
    // instance table lie about who's running. Now orphans are dropped and
    // on_event_heartbeat returns false so the HTTP layer can respond 410.
    ScopedTestEnv env;
    ResourceManager mgr;

    Heartbeat hb;
    hb.id = make_uuid();  // never registered via create_ability_instance
    hb.abilityName = "DummyAbility";
    hb.version = "1.0.0";
    hb.instanceName = "dummy";
    hb.state = LifecycleState::Running;
    hb.IPCPort = 12345;
    hb.abilityPort = 54321;

    CHECK_FALSE(mgr.on_event_heartbeat(hb));
    CHECK(!mgr.get_ability_instance(hb.id).has_value());
    CHECK(mgr.get_all_ability_instances().empty());
}

TEST_CASE("singleton constraint surfaces via active instance lookup") {
    ScopedTestEnv env;
    ResourceManager mgr;

    auto cr = make_template(ABILITY_CR_TEMPLATE_SINGLETON);
    auto cr_id_str = mgr.add_ability_cr(cr);
    auto template_opt = mgr.get_ability_cr(uuids::uuid::from_string(cr_id_str).value());
    REQUIRE(template_opt.has_value());
    REQUIRE(template_opt->spec->singleton.value_or(false));

    auto instance_id = mgr.create_ability_instance(*template_opt);

    Heartbeat hb;
    hb.id = instance_id;
    hb.abilityName = "SoloAbility";
    hb.version = "1.0.0";
    hb.instanceName = "solo";
    hb.state = LifecycleState::Running;
    CHECK(mgr.on_event_heartbeat(hb));

    auto active = mgr.get_active_instances_by_ability_name("SoloAbility");
    CHECK(active.size() == 1);

    // 此时若再创建一条同 template 的实例, 框架上层 (HTTP/auto_start) 应当拒绝。
    // 这里只验证检查依据: get_active_instances_by_ability_name 非空。
    CHECK(!active.empty());

    // 销毁后再次允许
    mgr.delete_ability_instance(instance_id);
    CHECK(mgr.get_active_instances_by_ability_name("SoloAbility").empty());
}

TEST_CASE("resolve_ability_cr_by_id round-trips spec snapshot for instance id") {
    ScopedTestEnv env;
    ResourceManager mgr;

    auto cr = make_template(ABILITY_CR_TEMPLATE);
    auto cr_id_str = mgr.add_ability_cr(cr);
    auto template_opt = mgr.get_ability_cr(uuids::uuid::from_string(cr_id_str).value());
    REQUIRE(template_opt.has_value());

    auto instance_id = mgr.create_ability_instance(*template_opt);
    auto resolved = mgr.resolve_ability_cr_by_id(instance_id);
    REQUIRE(resolved.has_value());
    CHECK(resolved->id == instance_id);
    CHECK(resolved->spec->abilityName == "DummyAbility");
    CHECK(resolved->spec->package == "dummy.pkg");

    // 同时也支持从 CR id 直接取模板 (回退路径)
    auto cr_by_id = mgr.resolve_ability_cr_by_id(template_opt->id);
    REQUIRE(cr_by_id.has_value());
    CHECK(cr_by_id->id == template_opt->id);
}

TEST_CASE("update() reconciles AbilityCRBasic against an empty crs/ dir") {
    // 验证 reconcile 行为: 通过 add_ability_cr 注入一个孤儿模板,
    // 然后让框架重新扫描 (空) crs/ 目录, 孤儿应被清理。
    // 这正是用户场景中 'mnwz9r4a' 那种历史脏数据被清掉的路径。
    ScopedTestEnv env;
    ResourceManager mgr;

    auto orphan = make_template(ABILITY_CR_TEMPLATE);
    orphan.metadata.name = "orphan-from-old-api";
    auto orphan_id_str = mgr.add_ability_cr(orphan);
    auto orphan_id = uuids::uuid::from_string(orphan_id_str).value();
    REQUIRE(mgr.get_ability_cr(orphan_id).has_value());

    // crs/ 是空目录 (ScopedTestEnv 创建时只 mkdir, 没放 yaml)
    mgr.update();

    CHECK(!mgr.get_ability_cr(orphan_id).has_value());
    // 全表应该没有任何 ability CR 残留
    CHECK(mgr.get_all_ability_cr().empty());
}

TEST_CASE("StoreManager::mirror_package_crs copies package crs/ into framework crs/_packages/") {
    ScopedTestEnv env;
    // 模拟一个已安装的包目录，内嵌 CR
    auto pkg_dir = env.home / "packages" / "dummy.pkg" / "1.0.0";
    fs::create_directories(pkg_dir / "crs");
    std::ofstream(pkg_dir / "crs" / "one.yaml") << ABILITY_CR_TEMPLATE;
    std::ofstream(pkg_dir / "crs" / "two.yaml") << ABILITY_CR_TEMPLATE_SINGLETON;
    // 子目录里也放一份, 验证递归
    fs::create_directories(pkg_dir / "crs" / "sub");
    std::ofstream(pkg_dir / "crs" / "sub" / "three.yaml") << ABILITY_CR_TEMPLATE;

    auto count = StoreManager::mirror_package_crs(pkg_dir, "dummy.pkg", "1.0.0");
    CHECK(count == 3);

    auto mirror_dir = StoreManager::mirrored_pkg_crs_dir("dummy.pkg", "1.0.0");
    CHECK(fs::exists(mirror_dir / "one.yaml"));
    CHECK(fs::exists(mirror_dir / "two.yaml"));
    CHECK(fs::exists(mirror_dir / "sub" / "three.yaml"));
}

TEST_CASE("StoreManager::unmirror_package_crs removes the version subtree") {
    ScopedTestEnv env;
    auto pkg_dir = env.home / "packages" / "dummy.pkg" / "1.0.0";
    fs::create_directories(pkg_dir / "crs");
    std::ofstream(pkg_dir / "crs" / "one.yaml") << ABILITY_CR_TEMPLATE;

    StoreManager::mirror_package_crs(pkg_dir, "dummy.pkg", "1.0.0");
    auto mirror_dir = StoreManager::mirrored_pkg_crs_dir("dummy.pkg", "1.0.0");
    REQUIRE(fs::exists(mirror_dir / "one.yaml"));

    StoreManager::unmirror_package_crs("dummy.pkg", "1.0.0");
    CHECK(!fs::exists(mirror_dir));
}

TEST_CASE("read_crs_into_db recursively walks crs/_packages/<pkg>/<version>/ subdirs") {
    // 不依赖 manifest 校验, 只验证递归扫描这一行为本身:
    // 在嵌套目录放一个非法的 yaml (没有 spec/package), 然后 update() 之后
    // cr_validation.log 会包含针对该文件的失败记录, 证明扫描确实下到了这一层。
    ScopedTestEnv env;
    ResourceManager mgr;

    auto nested = env.home / "crs" / "_packages" / "fake.pkg" / "9.9.9";
    fs::create_directories(nested);
    std::ofstream(nested / "broken.yaml") << R"yaml(
kind: AtomAbility
metadata:
  name: broken-one
spec:
  package: fake.pkg
  version: 9.9.9
  abilityName: NonExistent
  position: localhost
  config: {}
)yaml";

    mgr.update();

    auto log_path = env.home / "log" / "cr_validation.log";
    REQUIRE(fs::exists(log_path));
    std::ifstream log(log_path);
    std::string contents((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    CHECK(contents.find("broken.yaml") != std::string::npos);
    CHECK(contents.find("NonExistent") != std::string::npos);
}

TEST_CASE("Skill reads distinguish parent traversal from relative dot-prefixed names") {
    ScopedTestEnv env;
    const auto base = StoreManager::mirrored_pkg_skills_dir("dummy.pkg", "1.0.0");
    fs::create_directories(base);
    std::ofstream(base / "SKILL.md") << "valid";
    std::ofstream(base / "..notes.md") << "notes";
    std::ofstream(base.parent_path() / "outside.md") << "outside";
    CHECK(StoreManager::read_skill("dummy.pkg", "1.0.0", "SKILL.md") == "valid");
    CHECK(StoreManager::read_skill("dummy.pkg", "1.0.0", "..notes.md") == "notes");
    CHECK_FALSE(StoreManager::read_skill("dummy.pkg", "1.0.0", "../outside.md").has_value());
#ifdef _WIN32
    CHECK_FALSE(StoreManager::read_skill("dummy.pkg", "1.0.0", "..\\outside.md").has_value());
#endif
}
