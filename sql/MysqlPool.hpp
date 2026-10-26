#pragma once
#include <vector>
#include <queue>
#include <memory>
#include <condition_variable>
#include "MysqlConnection.hpp"


class MysqlPool {
public:
    using m_seconds = std::chrono::milliseconds;

    MysqlPool (
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& db,
        unsigned int port,
        size_t pool_size
    ) {
        for (size_t i = 0; i < pool_size; i++) {
            auto conn = std::make_unique<MysqlConnection>(
                host,
                user,
                password,
                db,
                port
            );

            MysqlConnection *raw = conn.get();
            _connections.push_back(std::move(conn));
            _idle.push(raw);
        }
    }

    class MysqlLease {
    public:
        MysqlLease() = default;
        MysqlLease(MysqlPool *pool, MysqlConnection *conn) : _pool(pool), _conn(conn) {}
        ~MysqlLease() {
            reset();
        }

        MysqlLease(const MysqlLease&) = delete;
        MysqlLease& operator=(const MysqlLease&) = delete;

        MysqlLease(MysqlLease &&other) noexcept : _pool(other._pool), _conn(other._conn) {
            other._pool = nullptr;
            other._conn = nullptr;
        }

        MysqlLease& operator=(MysqlLease&& other) noexcept {
            if (this != &other) {
                reset();

                _pool = other._pool;
                _conn = other._conn;

                other._pool = nullptr;
                other._conn = nullptr;
            }

            return *this;
        }

        MYSQL* get() {
            return _conn ? _conn->get() : nullptr;
        }

        MysqlConnection* operator->() {
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
            _pool = nullptr;
            _conn = nullptr;
        }


    private:
        MysqlConnection *_conn = nullptr;
        MysqlPool *_pool = nullptr;
    };


    MysqlLease acquire(m_seconds timeout) {
        MysqlConnection* conn = nullptr;
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
            return MysqlLease(this, conn);
        }
        
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _idle.push(conn);
        }
        _cv.notify_one();
        return {};
    }

    MysqlLease acquire() {
        MysqlConnection* conn = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] () -> bool {
                return !_idle.empty();
            });
            conn = _idle.front();
            _idle.pop();
        }

        if (conn->is_valid() || conn->reconnect()) {
            return MysqlLease(this, conn);
        }
        
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _idle.push(conn);
        }
        _cv.notify_one();
        return {};
    }

private:
    void release(MysqlConnection *conn) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _idle.push(conn);
        }
        _cv.notify_one();
    }

private:
    std::vector<std::unique_ptr<MysqlConnection>> _connections;
    std::queue<MysqlConnection*> _idle;
    std::mutex _mutex;
    std::condition_variable _cv;
};