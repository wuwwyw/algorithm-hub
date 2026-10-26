#include "../redis/RedisConnection.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto port = static_cast<unsigned int>(std::stoul(argv[1]));
    const char* password = std::getenv("TEST_REDIS_PASSWORD");
    assert(password && *password);
    RedisConnection connection("127.0.0.1", port, "", password);
    assert(connection.is_valid());
    assert(connection.reconnect() && connection.is_valid());
    const std::string key = "isolated-auth-test:%s with spaces";
    assert(connection.setex(key, 10, "test-value"));
    assert(connection.query(key).value() == "test-value");
    assert(connection.del(key));
    assert(!connection.query(key).has_value());
    RedisConnection acl("127.0.0.1", port, "default", password);
    assert(acl.is_valid());
    bool rejected = false;
    try { RedisConnection wrong("127.0.0.1", port, "", "intentionally-incorrect"); }
    catch (const std::runtime_error& error) {
        rejected = std::string(error.what()) == "redis authentication failed";
    }
    assert(rejected);
    std::cout << "Redis authentication, reconnect and literal-key checks passed\n";
}
