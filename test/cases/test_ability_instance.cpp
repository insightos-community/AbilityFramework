// Unit test verifying strict separation between CR (template) and Instance (runtime)
// - starting an instance does not modify the CR row
// - instance id differs from CR id
// - multiple instances can be derived from one template (singleton=false)
// - after delete the instance disappears from the table
// - instance is auto-destroyed when heartbeat enters Terminated
// - singleton constraint: activeInstancesByAbilityName reflects running instances
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

// Each test suite uses an independent framework home dir + in-memory database to avoid interference.
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

        // minimal config.yaml: only set framework_name, rest use defaults
        std::ofstream cfg(home / "config.yaml");
        cfg << "framework_name: test_fwk_" << unique << "\n";
        cfg.close();

        ::setenv("ABILITY_FRAMEWORK_HOME", home.c_str(), 1);
        // global_vars::init is an idempotent update; calling it multiple times is safe
        global_vars::init();

        try {
            database_mgr::init_database(home / "databases" / "test.db");
            owns_db = true;
        } catch (const std::exception&) {
            // multiple tests in the same process may reuse the global DB; the second init_database
            // will throw, just reuse
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

    // instance row exists, CR row still present
    auto info = mgr.get_ability_instance(instance_id);
    REQUIRE(info.has_value());
    CHECK(info->cr_id.has_value());
    CHECK(*info->cr_id == cr_id);
    CHECK(info->ability_name == "DummyAbility");
    CHECK(info->state == "Inactive");

    // template not overwritten by instance-creation action
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
    // default state is Inactive, so the active list is empty at this point
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

    // if another instance with the same template is created at this point, the framework upper layer (HTTP/auto_start) should reject it.
    // here we only verify the check basis: get_active_instances_by_ability_name is non-empty.
    CHECK(!active.empty());

    // allowed again after destruction
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

    // also supports taking the template directly from CR id (fallback path)
    auto cr_by_id = mgr.resolve_ability_cr_by_id(template_opt->id);
    REQUIRE(cr_by_id.has_value());
    CHECK(cr_by_id->id == template_opt->id);
}

TEST_CASE("update() reconciles AbilityCRBasic against an empty crs/ dir") {
    // verify reconcile behavior: inject an orphan template via add_ability_cr,
    // then let the framework rescan the (empty) crs/ directory; orphans should be cleaned up.
    // this is exactly the path where historical dirty data like 'mnwz9r4a' gets cleaned.
    ScopedTestEnv env;
    ResourceManager mgr;

    auto orphan = make_template(ABILITY_CR_TEMPLATE);
    orphan.metadata.name = "orphan-from-old-api";
    auto orphan_id_str = mgr.add_ability_cr(orphan);
    auto orphan_id = uuids::uuid::from_string(orphan_id_str).value();
    REQUIRE(mgr.get_ability_cr(orphan_id).has_value());

    // crs/ is an empty dir (ScopedTestEnv only does mkdir when created, no yaml placed)
    mgr.update();

    CHECK(!mgr.get_ability_cr(orphan_id).has_value());
    // the whole table should have no ability CR residue
    CHECK(mgr.get_all_ability_cr().empty());
}

TEST_CASE("StoreManager::mirror_package_crs copies package crs/ into framework crs/_packages/") {
    ScopedTestEnv env;
    // simulate an installed package directory with embedded CR
    auto pkg_dir = env.home / "packages" / "dummy.pkg" / "1.0.0";
    fs::create_directories(pkg_dir / "crs");
    std::ofstream(pkg_dir / "crs" / "one.yaml") << ABILITY_CR_TEMPLATE;
    std::ofstream(pkg_dir / "crs" / "two.yaml") << ABILITY_CR_TEMPLATE_SINGLETON;
    // also place one in a subdirectory to verify recursion
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
    // does not depend on manifest validation, only verifies the recursive-scanning behavior itself:
    // place an illegal yaml (no spec/package) in a nested directory, then after update()
    // cr_validation.log will contain a failure record for that file, proving the scan did reach this level.
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
