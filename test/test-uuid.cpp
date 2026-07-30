#include <string>
#include <functional>
#include <sstream>
#include <uuid.h>
#include <random>
#include <iostream>

namespace detail {

// generate deterministic seed based on name and version
inline std::size_t generate_seed(const std::string& name, const std::string& version) {
    std::hash<std::string> hasher;
    auto res = hasher(name) ^ (hasher(version) << 1);
    std::cout << "Result: " << res << std::endl;
    return res;
}

// initialize RNG from seed
inline auto initialize_generator_with_seed(std::size_t seed) {
    std::mt19937 gen(seed);
    return gen;
}

} // namespace detail

// function to generate UUID based on name and version
inline uuids::uuid make_uuid(const std::string& name, const std::string& version) {
    auto seed = detail::generate_seed(name, version);
    std::mt19937 generator = detail::initialize_generator_with_seed(seed);
    uuids::uuid_random_generator g(generator);
    return g();
}

// test main function
int main() {
    std::string name1 = "example-crd";
    std::string version1 = "v1.0";

    std::string name2 = "example";
    std::string version2 = "v2.0";

    // generate UUID using same name and version
    uuids::uuid uuid1 = make_uuid(name1, version1);
    uuids::uuid uuid2 = make_uuid(name1, version1);

    // generate UUID using different name or version
    uuids::uuid uuid3 = make_uuid(name2, version1);
    uuids::uuid uuid4 = make_uuid(name1, version2);
    uuids::uuid uuid5 = make_uuid(name1, version2);
    uuids::uuid uuid6 = make_uuid(name2, version2);

    std::cout << "UUID 1 (same name/version): " << uuid1 << '\n';
    std::cout << "UUID 2 (same name/version): " << uuid2 << '\n';
    std::cout << "UUID 3 (different name): " << uuid3 << '\n';
    std::cout << "UUID 4 (different version): " << uuid4 << '\n';
    std::cout << "UUID 5 (different version): " << uuid5 << '\n';
    std::cout << "UUID 6 (different version): " << uuid6 << '\n';

    return 0;
}
