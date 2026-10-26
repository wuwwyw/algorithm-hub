#pragma once
#include <optional>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <openssl/rand.h>
#include "RedisPool.hpp"
using Headers = std::unordered_map <std::string, std::string>;

class Session_manager {
public:
    static constexpr int SESSION_TTL = 7 * 24 * 60 * 60;

    Session_manager(RedisPool &redis_pool) : _redis_pool(redis_pool) {}
    
    std::optional<std::string> create(long long user_id) {
        const std::string session_id = generate_session_id();
        const std::string key = "session:" + session_id;

        auto conn = _redis_pool.acquire(RedisPool::m_seconds(1000));
        if (!conn || !conn->setex(key, SESSION_TTL, std::to_string(user_id))) {
            return std::nullopt;
        }
        return session_id;
    }

    std::optional<long long> get_user_id(const Headers &headers) {
        auto session_id = get_Cookie(headers, "session_id");
        if (!session_id) return std::nullopt;

        auto conn = _redis_pool.acquire(RedisPool::m_seconds(1000));
        if (!conn) {
            return std::nullopt;
        }

        auto value = conn->query("session:" + *session_id);
        if (!value) return std::nullopt;
        
        try {
            return std::stoll(*value);
        } catch (...) {
            return std::nullopt;
        }
    }

    bool destroy(const Headers &headers) {
        auto session_id = get_Cookie(headers, "session_id");
        if (!session_id) return true;

        auto conn = _redis_pool.acquire(RedisPool::m_seconds(1000));
        if (!conn) {
            return false;
        }

        return conn->del("session:" + *session_id);
    }

    std::string make_Cookie(const std::string& session_id) {
        return "session_id=" + session_id + "; Path=/" + "; HttpOnly" + "; SameSite=Lax" + "; Max-Age=" + std::to_string(SESSION_TTL);
    }

    std::string expire_cookie() {
        return std::string("session_id=") + "; Path=/" + "; HttpOnly" + "; SameSite=Lax" + "; Max-Age=0";
    }

private:
    std::string generate_session_id() {
        unsigned char bytes[32];
        if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
            throw std::runtime_error("generate session id failed");
        }

        std::ostringstream oss;
        for (unsigned char byte : bytes) {
            oss << std::hex
                << std::setw(2)
                << std::setfill('0')
                << static_cast<int>(byte);
        }
        return oss.str();
    }

    std::optional<std::string> get_Cookie(const Headers &headers, const std::string &target) {
        auto it = headers.find("Cookie");
        if (it == headers.end()) {
            return std::nullopt;
        }

        std::stringstream ss(it->second);
        std::string item;
        while (getline(ss, item, ';')) {
            while (!item.empty() && item.front() == ' ') {
                item.erase(item.begin());
            }

            size_t pos = item.find('=');
            if (pos == std::string::npos) continue;
            std::string name = item.substr(0, pos);
            std::string value = item.substr(pos + 1);
            if (name == target) {
                return value;
            }
        }
        return std::nullopt;
    }

private:
    RedisPool &_redis_pool;
};