#include "resourcemgr/resource_mgr.hpp"
#include <chrono>
#include <thread>

int main() {
    ResourceManager mgr;
    std::filesystem::path crPath = std::filesystem::current_path() / "cr";
    while(true) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    return 0;
}