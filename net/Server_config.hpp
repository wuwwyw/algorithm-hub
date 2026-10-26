#pragma once
#include <arpa/inet.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#ifndef ALGORITHM_HUB_CONFIG_ROOT
#define ALGORITHM_HUB_CONFIG_ROOT "."
#endif

// Environment > selected .env file > defaults. Passwords have no built-in default.
struct ServerConfig {
    std::string listen_host = "127.0.0.1";
    unsigned int listen_port = 8080;
    std::string db_host = "127.0.0.1";
    unsigned int db_port = 3306;
    std::string db_user = "algorithm_app";
    std::string db_password;
    std::string db_name = "algorithm_hub";
    std::string redis_host = "127.0.0.1";
    unsigned int redis_port = 6379;
    std::string redis_user;
    std::string redis_password;

    static ServerConfig load() {
        const char* selected = std::getenv("ALGORITHM_CONFIG_FILE");
        const bool explicit_file = selected && *selected;
        const std::filesystem::path path = explicit_file
            ? std::filesystem::path(selected)
            : std::filesystem::path(ALGORITHM_HUB_CONFIG_ROOT) / ".env";
        const auto values = read_file(path, explicit_file);
        auto get = [&](const char* key, const std::string& fallback) {
            if (const char* value = std::getenv(key)) return std::string(value);
            auto it = values.find(key);
            return it == values.end() ? fallback : it->second;
        };
        ServerConfig config;
        config.listen_host = get("ALGORITHM_HOST", config.listen_host);
        config.listen_port = parse_port(get("ALGORITHM_PORT", "8080"), "ALGORITHM_PORT");
        config.db_host = get("ALGORITHM_DB_HOST", config.db_host);
        config.db_port = parse_port(get("ALGORITHM_DB_PORT", "3306"), "ALGORITHM_DB_PORT");
        config.db_user = get("ALGORITHM_DB_USER", config.db_user);
        config.db_password = get("ALGORITHM_DB_PASSWORD", "");
        config.db_name = get("ALGORITHM_DB_NAME", config.db_name);
        config.redis_host = get("ALGORITHM_REDIS_HOST", config.redis_host);
        config.redis_port = parse_port(get("ALGORITHM_REDIS_PORT", "6379"), "ALGORITHM_REDIS_PORT");
        config.redis_user = get("ALGORITHM_REDIS_USER", "");
        config.redis_password = get("ALGORITHM_REDIS_PASSWORD", "");
        require(config.listen_host, "ALGORITHM_HOST");
        in_addr address{};
        if (inet_pton(AF_INET, config.listen_host.c_str(), &address) != 1) {
            throw std::runtime_error("ALGORITHM_HOST must be a valid IPv4 address");
        }
        require(config.db_host, "ALGORITHM_DB_HOST");
        require(config.db_user, "ALGORITHM_DB_USER");
        require(config.db_name, "ALGORITHM_DB_NAME");
        require(config.db_password, "ALGORITHM_DB_PASSWORD");
        require(config.redis_host, "ALGORITHM_REDIS_HOST");
        if (!config.redis_user.empty() && config.redis_password.empty()) {
            throw std::runtime_error("ALGORITHM_REDIS_USER requires ALGORITHM_REDIS_PASSWORD");
        }
        return config;
    }

    static unsigned int parse_port(const std::string& value, const char* key) {
        if (value.empty() || value.size() > 5 || value.find_first_not_of("0123456789") != std::string::npos) {
            throw std::runtime_error(std::string(key) + " must be an integer from 1 to 65535");
        }
        const auto port = std::stoul(value);
        if (port == 0 || port > 65535) {
            throw std::runtime_error(std::string(key) + " must be an integer from 1 to 65535");
        }
        return static_cast<unsigned int>(port);
    }

private:
    static void require(const std::string& value, const char* key) {
        if (value.empty()) {
            throw std::runtime_error(std::string("Missing ") + key + "; configure the environment or .env file");
        }
    }

    static std::string trim(const std::string& value) {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return "";
        return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
    }

    static std::unordered_map<std::string, std::string> read_file(
            const std::filesystem::path& path, bool required) {
        std::ifstream input(path);
        if (!input) {
            if (required || std::filesystem::exists(path)) {
                throw std::runtime_error("Cannot read configuration file: " + path.string());
            }
            return {};
        }
        std::unordered_map<std::string, std::string> values;
        std::string line;
        size_t number = 0;
        while (std::getline(input, line)) {
            ++number;
            if (number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            line = trim(line);
            if (line.empty() || line.front() == '#') continue;
            const auto equal = line.find('=');
            if (equal == std::string::npos) {
                throw std::runtime_error("Invalid configuration syntax at line " + std::to_string(number));
            }
            const auto key = trim(line.substr(0, equal));
            if (key.empty() || key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) {
                throw std::runtime_error("Invalid configuration key at line " + std::to_string(number));
            }
            auto value = trim(line.substr(equal + 1));
            if (!value.empty() && (value.front() == '\'' || value.front() == '"')) {
                if (value.size() < 2 || value.back() != value.front()) {
                    throw std::runtime_error("Unclosed configuration quote at line " + std::to_string(number));
                }
                value = value.substr(1, value.size() - 2);
            }
            // Literal values: no shell execution, interpolation or escape processing.
            values[key] = value;
        }
        if (!input.eof()) throw std::runtime_error("Error reading configuration file: " + path.string());
        return values;
    }
};
