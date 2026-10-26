#pragma once
#include <hiredis/hiredis.h>
#include <optional>
#include <stdexcept>
#include <string>

const unsigned int def_redis_port = 6379;

class RedisConnection {
public:
    RedisConnection(const std::string &host, unsigned int port,
                    const std::string& username = "", const std::string& password = "")
        : _host(host), _port(port), _username(username), _password(password) {
        connect();
    }
    ~RedisConnection() {
        close();
    }

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;

    bool setex(const std::string &key, int seconds, const std::string &val) {
        if (_ctx == nullptr) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(_ctx, "SET %s %s EX %d", key.c_str(), val.c_str(), seconds));
        if (!reply) return false;
        bool ok = reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK";
        freeReplyObject(reply);
        return ok;
    }

    std::optional<std::string> query(const std::string &key) {
        if (_ctx == nullptr) {
            return std::nullopt;
        }

        redisReply* reply = static_cast<redisReply*>(key == "PING"
            ? redisCommand(_ctx, "PING")
            : redisCommand(_ctx, "GET %b", key.data(), key.size()));
        if (!reply) {
            return std::nullopt;
        }
        if (reply->type != REDIS_REPLY_STRING && reply->type != REDIS_REPLY_STATUS) {
            freeReplyObject(reply);
            return std::nullopt;
        }

        std::string value(reply->str, reply->len);
        freeReplyObject(reply);
        return value;
    }

    bool del(const std::string& key) {
        if (_ctx == nullptr) {
            return false;
        }
        
        redisReply* reply = static_cast<redisReply*>(redisCommand(_ctx, "DEL %s", key.c_str()));

        if (!reply) {
            return false;
        }

        bool ok = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return ok;
    }

    bool is_valid() {
        auto ping = query("PING");
        if (!ping) return false;
        return *ping == "PONG";
    }

    bool reconnect() {
        close();

        try {
            connect();
            return true;
        } catch(...) {
            return false;
        }
    }

    redisContext* get() {
        return _ctx;
    }

private:
    void connect() {
        _ctx = redisConnect(_host.c_str(), _port);

        if (_ctx == nullptr) {
            throw std::runtime_error("redis connect failed");
        }

        if (_ctx->err) {
            std::string msg = _ctx->errstr;
            redisFree(_ctx);
            _ctx = nullptr;
            throw std::runtime_error(
                "redis connect failed: " + msg
            );
        }
        if (!_password.empty()) {
            auto* reply = static_cast<redisReply*>(_username.empty()
                ? redisCommand(_ctx, "AUTH %b", _password.data(), _password.size())
                : redisCommand(_ctx, "AUTH %b %b", _username.data(), _username.size(),
                               _password.data(), _password.size()));
            const bool authenticated = reply && reply->type == REDIS_REPLY_STATUS
                && std::string(reply->str, reply->len) == "OK";
            if (reply) freeReplyObject(reply);
            if (!authenticated) {
                close();
                throw std::runtime_error("redis authentication failed");
            }
        }
    }

    void close() {
        if (_ctx) {
            redisFree(_ctx);
            _ctx = nullptr;
        }
    }

private:
    std::string _host;
    unsigned int _port;
    std::string _username;
    std::string _password;

    redisContext* _ctx{};
};
