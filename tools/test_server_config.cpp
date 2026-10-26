#include "../net/Server_config.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>

int main() {
    for (const char* key : {"ALGORITHM_HOST", "ALGORITHM_PORT", "ALGORITHM_DB_HOST", "ALGORITHM_DB_PORT",
                           "ALGORITHM_DB_USER", "ALGORITHM_DB_PASSWORD", "ALGORITHM_DB_NAME", "ALGORITHM_REDIS_HOST",
                           "ALGORITHM_REDIS_PORT", "ALGORITHM_REDIS_USER", "ALGORITHM_REDIS_PASSWORD"}) {
        unsetenv(key);
    }
    const auto path = std::filesystem::temp_directory_path() / ("algorithm-config-" + std::to_string(getpid()) + ".env");
    setenv("ALGORITHM_CONFIG_FILE", path.c_str(), 1);
    auto write = [&](const std::string& text) { std::ofstream file(path); file << text; };
    auto must_fail = [&](const std::string& field) {
        bool failed = false;
        try { (void)ServerConfig::load(); }
        catch (const std::runtime_error& error) {
            failed = std::string(error.what()).find(field) != std::string::npos;
        }
        assert(failed);
    };
    write("\xef\xbb\xbf# CRLF and literal values\r\nALGORITHM_DB_PASSWORD=\"literal#$(not-executed)\"\r\n"
          "ALGORITHM_DB_USER=first\r\nALGORITHM_DB_USER=file_user\r\n");
    auto config = ServerConfig::load();
    assert(config.db_user == "file_user" && config.db_password == "literal#$(not-executed)");
    assert(config.listen_host == "127.0.0.1" && config.db_port == 3306 && config.redis_port == 6379);
    setenv("ALGORITHM_DB_USER", "environment_user", 1);
    assert(ServerConfig::load().db_user == "environment_user");
    setenv("ALGORITHM_HOST", "not-an-ip", 1);
    must_fail("ALGORITHM_HOST");
    unsetenv("ALGORITHM_HOST");
    for (const char* bad : {"0", "65536", "-1", "8080junk", ""}) {
        setenv("ALGORITHM_DB_PORT", bad, 1);
        must_fail("ALGORITHM_DB_PORT");
    }
    setenv("ALGORITHM_DB_PORT", "13306", 1);
    assert(ServerConfig::load().db_port == 13306);
    unsetenv("ALGORITHM_DB_PORT");
    setenv("ALGORITHM_DB_PASSWORD", "", 1);
    must_fail("ALGORITHM_DB_PASSWORD");
    unsetenv("ALGORITHM_DB_PASSWORD");
    setenv("ALGORITHM_REDIS_USER", "app", 1);
    must_fail("ALGORITHM_REDIS_PASSWORD");
    unsetenv("ALGORITHM_REDIS_USER");
    write("ALGORITHM_DB_PASSWORD=\"unclosed\n");
    must_fail("quote");
    write("missing_equal\n");
    must_fail("syntax");
    std::filesystem::remove(path);
    must_fail("Cannot read");
    std::cout << "Server configuration checks passed\n";
}
