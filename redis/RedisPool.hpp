#pragma once
#include <memory>
#include <vector>
#include <queue>
#include <condition_variable>
#include "RedisConnection.hpp"


class RedisPool {
public:
    using m_seconds = std::chrono::milliseconds;
    
    RedisPool (const std::string &host, const unsigned int port, size_t pool_size,
               const std::string& username = "", const std::string& password = "") {
        for (size_t i = 0; i < pool_size; i++) {
            auto conn = std::make_unique<RedisConnection>(host, port, username, password);

            RedisConnection *raw = conn.get();
            _connections.push_back(std::move(conn));
            _idle.push(raw);
        }
    }

    class RedisConnGuard {
    public:
        RedisConnGuard() = default;
        RedisConnGuard(RedisPool *pool, RedisConnection *conn) : _conn(conn), _pool(pool) {}
        ~RedisConnGuard() {
            reset();
        }

        RedisConnGuard(const RedisConnGuard&) = delete;
        RedisConnGuard& operator=(const RedisConnGuard&) = delete;

        RedisConnGuard(RedisConnGuard &&other) noexcept : _conn(other._conn), _pool(other._pool) {
            other._conn = nullptr;
            other._pool = nullptr;
        }

        RedisConnGuard& operator=(RedisConnGuard &&other) noexcept {
            if (this != &other) {
                reset();
                _conn = other._conn;
                _pool = other._pool;
                other._conn = nullptr;
                other._pool = nullptr;
            }
            return *this;
        }

        redisContext* get() const noexcept {
            return _conn ? _conn->get() : nullptr;
        }

        RedisConnection *operator->() const noexcept {
            return _conn;
        }

        explicit operator bool() const {
            return _conn != nullptr;
        }

    private:
        void reset() {
            if (_conn && _pool) {
                _pool->release(_conn);
            }

            _conn = nullptr;
            _pool = nullptr;
        }

    private:
        RedisPool *_pool = nullptr;
        RedisConnection *_conn = nullptr;
    };

    RedisConnGuard acquire(m_seconds timeout) {
        RedisConnection *conn;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            bool success = _cv.wait_for(lock, timeout, [this] () -> bool {
                return !_idle.empty();
            });

            if (!success) return {};
            conn = _idle.front();
            _idle.pop();
        }

        if (conn->is_valid() || conn->reconnect()) {
            return RedisConnGuard(this, conn);
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _idle.push(conn);
        }
        _cv.notify_one();
        return {};
    }

    RedisConnGuard acquire() {
        RedisConnection *conn;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] () -> bool {
                return !_idle.empty();
            });
            conn = _idle.front();
            _idle.pop();
        }

        if (conn->is_valid() || conn->reconnect()) {
            return RedisConnGuard(this, conn);
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _idle.push(conn);
        }
        _cv.notify_one();
        return {};
    }

private:
    void release(RedisConnection *conn) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _idle.push(conn);
        }
        _cv.notify_one();
    }

private:
    std::vector<std::unique_ptr<RedisConnection>> _connections;
    std::queue<RedisConnection*> _idle;
    std::mutex _mutex;
    std::condition_variable _cv;
};
