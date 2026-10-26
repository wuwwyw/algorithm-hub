// Isolated read-only probe; run only against the local development Redis.
#include "../redis/RedisConnection.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    RedisConnection connection("127.0.0.1", 6379);
    auto value = connection.query(std::string("session:security_audit_nonexistent_") + argv[1]);
    // Never output values returned from Redis.
    std::cout << "query_completed=" << (value.has_value() ? "value" : "no_value") << '\n';
}
