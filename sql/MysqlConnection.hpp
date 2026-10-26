#pragma once
#include <mysql/mysql.h>
#include <iostream>

const unsigned int def_mysql_port = 3306;

class MysqlConnection {
public:
    MysqlConnection(
        const std::string &host,
        const std::string &user,
        const std::string &password,
        const std::string &db,
        unsigned int port
    ) : _host(host), _user(user), _password(password), _db(db), _port(port) {
        connect();
    }
    ~MysqlConnection() {
        close();
    }

    bool query(const std::string& sql) {
        if (!_conn) return false;
        if (mysql_query(_conn, sql.c_str()) != 0) {
            std::cerr << "query failed: " << mysql_error(_conn) << std::endl;
            return false;
        }
        return true;
    }

    MYSQL_RES* get_result() {
        if (!_conn) return nullptr;
        return mysql_store_result(_conn);
    }

    bool is_valid() {
        return ping();
    }

    bool reconnect() {
        close();

        try {
            connect();
            return true;
        } catch (...) {
            return false;
        }
    }

    MYSQL* get() {
        return _conn;
    }

    const MYSQL* get() const {
        return _conn;
    }

private:
    void connect() {
        _conn = mysql_init(nullptr);
        if (!_conn) {
            throw std::runtime_error("mysql_init failed");
        }

        if (!mysql_real_connect(_conn, _host.c_str(), _user.c_str(), _password.c_str(), _db.c_str(), _port, nullptr, 0)) {
            std::string err = mysql_error(_conn);
            mysql_close(_conn);
            _conn = nullptr;
            throw std::runtime_error(
                "mysql_real_connect failed: " + err
            );
        }

        if (mysql_set_character_set(_conn, "utf8mb4") != 0) {
            std::string err = mysql_error(_conn);
            mysql_close(_conn);
            _conn = nullptr;
            throw std::runtime_error(
                "mysql_set_character_set failed: " + err
            );
        }
    }

    void close() {
        if (_conn) {
            mysql_close(_conn);
            _conn = nullptr;
        }
    }

    bool ping() {
        if (!_conn) {
            return false;
        }

        return mysql_ping(_conn) == 0;
    }

private:
    std::string _host;
    std::string _user;
    std::string _password;
    std::string _db;
    unsigned int _port;

    MYSQL *_conn = nullptr;
};