#pragma once
#include <cerrno>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <functional>
#include <unistd.h>
#include <memory>
#include "Http_response.hpp"
#include "../sql/Api_handle.hpp"
#include "../sql/Board_page.hpp"

#ifndef ALGORITHM_HUB_WEB_ROOT
#define ALGORITHM_HUB_WEB_ROOT "web"
#endif

using route_handler_t = std::function<Http_response(
    const std::vector<std::string> &params, 
    const std::string &body,
    const Headers &headers
)>;

class Trie_route {
public:
    explicit Trie_route(MysqlPool &mysql_pool, RedisPool &redis_pool, bool register_routes = true) : 
    _web_root(ALGORITHM_HUB_WEB_ROOT), 
    _mysql_pool(mysql_pool),
    _redis_pool(redis_pool) {
        if (register_routes) {
            _api_pub = std::make_unique<PublicBoardsApi>(_mysql_pool);
            _api_per = std::make_unique<PersonalBoardApi>(_mysql_pool);
            _api_user = std::make_unique<UserApi>(_mysql_pool, _session);
            register_static_routes();
            register_api_routes();
        }
    }

    Http_response handle(const std::string &method, const std::string &path, const std::string &body, const Headers& headers) {
        std::string clean_path = path;
        auto qpos = clean_path.find('?');
        if (qpos != std::string::npos) {
            clean_path.resize(qpos);
        }

        std::vector<std::string> parts = split_path(clean_path);
        std::vector<std::string> params;
        Trie_route *start = this;
        Trie_route *node = this;
        for (const std::string &part : parts) {
            auto it = node->children.find(part);
            if (it != node->children.end()) {
                node = it->second.get();
            } else if (node->param_child) {
                params.push_back(part);
                node = node->param_child.get();
            } else {
                return start->handle("GET", "/html/404.html", "", headers);
            }
        }
        auto handler = node->handlers.find(method);
        if (handler == node->handlers.end()) {
            return method_not_allowed_response();
        }
        return handler->second(params, body, headers);
    }

private:
    void register_static_routes() {
        register_static("GET", "/", "html/index.html");
        register_static("GET", "/html/index.html", "html/index.html");
        register_static("GET", "/css/index.css", "css/index.css");

        register_static("GET", "/html/login.html", "html/login.html");
        register_static("GET", "/css/login.css", "css/login.css");

        register_static("GET", "/html/register.html", "html/register.html");
        register_static("GET", "/css/register.css", "css/register.css");

        register_static("GET", "/html/my-boards.html", "html/my-boards.html");
        register_static("GET", "/css/my-boards.css", "css/my-boards.css");

        register_static("GET", "/html/404.html", "html/404.html");
        register_static("GET", "/css/404.css", "css/404.css");

        register_static("GET", "/html/500.html", "html/500.html");
        register_static("GET", "/css/500.css", "css/500.css");
    }

    void register_static(const std::string &method, const std::string &url, const std::filesystem::path &file) {
        register_handler(method, url, [this, url, file] (const std::vector<std::string> &params, const std::string &body, const Headers &headers) -> Http_response {
            std::string response_body;
            if (!file_read(file, response_body)) {
                if (url.size() >= 8 && url.substr(url.size() - 8) == "500.html") {
                    Http_response response(500, "Internal Server Error", "500 Internal Server Error");
                    response.add_header("Content-Type", "text/plain");
                    return response;
                }
                return handle("GET", "/html/500.html", "", headers);
            }
            if (url.size() >= 8 && url.substr(url.size() - 8) == "500.html") {
                return Http_response(500, "Internal Server Error", response_body, file);
            } else if (url.size() >= 8 && url.substr(url.size() - 8) == "404.html") {
                return Http_response(404, "Not Found", response_body, file);
            } else {
                return Http_response(200, "OK", response_body, file);
            }
        });
    }

    void register_api_routes() {
        register_handler("POST", "/api/boards/page", [this] (const auto&, const std::string &body, const auto&) {
            return board_page::respond(_mysql_pool, std::nullopt, body);
        });
        register_handler("POST", "/api/my/boards/page", [this] (const auto&, const std::string &body, const Headers &headers) {
            auto user_id = _session.get_user_id(headers);
            if (!user_id) return _api_user->json_response(401, "Unauthorized", R"({"message":"not logged in"})");
            return board_page::respond(_mysql_pool, *user_id, body);
        });

        
        register_handler("POST", "/api/register", [this] (const auto&, const std::string &body, const auto&) -> Http_response {
            return _api_user->register_user(body);
        });

        register_handler("POST", "/api/login", [this] (const auto&, const std::string &body, const auto&) -> Http_response {
            return _api_user->login_user(body);
        });

        register_handler("POST", "/api/logout", [this] (const auto&, const auto&, const Headers &headers) -> Http_response {
            return _api_user->logout_user(headers);
        });

        register_handler("GET", "/api/me", [this] (const auto&, const auto&, const Headers &headers) -> Http_response {
            return _api_user->me(headers);
        });


        register_handler("GET", "/api/boards", [this] (const auto&, const auto&, const auto&) -> Http_response {
            return _api_pub->get_boards();
        });

        register_handler("GET", "/api/boards/{id}", [this] (const std::vector<std::string> &params, const auto&, const auto&) -> Http_response {
            if (params.size() != 1) {
                return _api_pub->json_response(400, "Bad Request", R"({"code":400, "message":"invalid parameter"})");
            }

            long long board_id;
            try {
                board_id = std::stoll(params[0]);
            } catch (...) {
                return _api_pub->json_response(400, "Bad Request", R"({"code":400, "message":"invalid board id"})");
            }
            return _api_pub->get_board(board_id);
        });

        register_handler("POST", "/api/boards/{id}/join", [this] (
            const std::vector<std::string> &params, 
            const auto&, 
            const Headers &headers) -> Http_response {
            
            auto user_id = _session.get_user_id(headers);
            if (!user_id) {
                return _api_user->json_response(
                    401,
                    "Unauthorized",
                    R"({"code":401,"message":"not logged in"})"
                );
            }

            if (params.size() != 1) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid parameter"})");
            }
            long long board_id;
            try {
                board_id = std::stoll(params[0]);
            } catch (...) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid board id"})");
            }

            return _api_per->join_board(*user_id, board_id);
        });


        register_handler("GET", "/api/my/boards", [this] (const auto&, const auto&, const Headers &headers) -> Http_response {
            auto user_id = _session.get_user_id(headers);
            if (!user_id) {
                return _api_user->json_response(
                    401,
                    "Unauthorized",
                    R"({"code":401,"message":"not logged in"})"
                );
            }

            return _api_per->get_boards(*user_id);
        });

        register_handler("GET", "/api/my/boards/{id}", [this] (const std::vector<std::string> &params, const auto&, const Headers &headers) -> Http_response {
            auto user_id = _session.get_user_id(headers);
            if (!user_id) {
                return _api_user->json_response(
                    401,
                    "Unauthorized",
                    R"({"code":401,"message":"not logged in"})"
                );
            }

            if (params.size() != 1) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid parameter"})");
            }
            long long board_id;
            try {
                board_id = std::stoll(params[0]);
            } catch (...) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid board id"})");
            }

            return _api_per->get_board(*user_id, board_id);
        });

        register_handler("POST", "/api/my/boards", [this] (const auto&, const std::string &body, const Headers &headers) -> Http_response {
            auto user_id = _session.get_user_id(headers);
            if (!user_id) {
                return _api_user->json_response(
                    401,
                    "Unauthorized",
                    R"({"code":401,"message":"not logged in"})"
                );
            }

            return _api_per->create_board(*user_id, body);
        });

        register_handler("POST", "/api/my/boards/export/pdf", [this] (
            const auto&, 
            const std::string &body, 
            const Headers &headers
        ) -> Http_response {
            auto user_id = _session.get_user_id(headers);
            if (!user_id) {
                return _api_user->json_response(
                    401,
                    "Unauthorized",
                    R"({"code":401,"message":"not logged in"})"
                );
            }

            return _api_per->export_pdf(*user_id, body);
        });

        register_handler("PUT", "/api/my/boards/{id}", [this] (
            const std::vector<std::string> &params, 
            const std::string &body, 
            const Headers &headers) -> Http_response {

            auto user_id = _session.get_user_id(headers);
            if (!user_id) {
                return _api_user->json_response(
                    401,
                    "Unauthorized",
                    R"({"code":401,"message":"not logged in"})"
                );
            }

            if (params.size() != 1) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid parameter"})");
            }
            long long board_id;
            try {
                board_id = std::stoll(params[0]);
            } catch (...) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid board id"})");
            }

            return _api_per->update_board(*user_id, board_id, body);
        });

        register_handler("DELETE", "/api/my/boards/{id}", [this] (
            const std::vector<std::string> &params, 
            const auto&, const Headers 
            &headers) -> Http_response {

            auto user_id = _session.get_user_id(headers);
            if (!user_id) {
                return _api_user->json_response(
                    401,
                    "Unauthorized",
                    R"({"code":401,"message":"not logged in"})"
                );
            }

            if (params.size() != 1) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid parameter"})");
            }
            long long board_id;
            try {
                board_id = std::stoll(params[0]);
            } catch (...) {
                return _api_per->json_response(400, "Bad Request", R"({"code":400, "message":"invalid board id"})");
            }

            return _api_per->delete_board(*user_id, board_id);
        });
    }

    
    void register_handler(const std::string &method, const std::string &path, route_handler_t handler) {
        std::vector<std::string> parts = split_path(path);
        Trie_route *node = this;
        for (const std::string &part : parts) {
            if (is_parameter(part)) {
                if (!node->param_child)
                {
                    node->param_child = std::unique_ptr<Trie_route>(new Trie_route(_mysql_pool, _redis_pool, false));
                }
                node = node->param_child.get();
            } else {
                if (node->children.find(part) == node->children.end()) {
                    std::unique_ptr<Trie_route> new_node(new Trie_route(_mysql_pool, _redis_pool, false));
                    node->children.emplace(part, std::move(new_node));
                }
                node = node->children[part].get();
            }
        }
        node->handlers[method] = std::move(handler);
    }

    std::vector <std::string> split_path(const std::string &path) {
        std::vector <std::string> result;
        size_t pre = 0;
        while (pre < path.size()) {
            while (pre < path.size() && path[pre] == '/') pre++;
            if (pre >= path.size()) break;
            size_t suf;
            suf = path.find('/', pre);
            if (suf == std::string::npos) {
                suf = path.size();
            }
            result.push_back(path.substr(pre, suf - pre));
            pre = suf;
        }
        return result;
    }

    bool file_read(const std::filesystem::path& relative_path, std::string& body) const {
        const std::filesystem::path file_path = _web_root / relative_path;
        const int fd = open(file_path.c_str(), O_RDONLY);
        if (fd == -1) {
            return false;
        }

        struct stat file_stat {};
        if (fstat(fd, &file_stat) == -1 || file_stat.st_size < 0) {
            close(fd);
            return false;
        }

        body.resize(static_cast<std::size_t>(file_stat.st_size));
        std::size_t total = 0;
        while (total < body.size()) {
            const ssize_t bytes_read = read(fd, body.data() + total, body.size() - total);
            if (bytes_read == -1 && errno == EINTR) {
                continue;
            }
            if (bytes_read <= 0) {
                body.clear();
                close(fd);
                return false;
            }
            total += static_cast<std::size_t>(bytes_read);
        }

        if (close(fd) == -1) {
            body.clear();
            return false;
        }
        return true;
    }

    bool is_parameter(const std::string &part) {
        return part.size() >= 2 && part.front() == '{' && part.back() == '}';
    }

    Http_response method_not_allowed_response() {
        Http_response response(
            405,
            "Method Not Allowed",
            R"({"code":405,"message":"method not allowed"})"
        );
        response.add_header("Content-Type", "application/json");
        return response;
    }

private:
    std::filesystem::path _web_root;
    std::unordered_map<std::string, std::unique_ptr<Trie_route>> children;
    std::unique_ptr<Trie_route> param_child;
    std::unordered_map<std::string, route_handler_t> handlers;
    
    MysqlPool &_mysql_pool;
    RedisPool &_redis_pool;
    Session_manager _session{_redis_pool};
    std::unique_ptr<PublicBoardsApi> _api_pub;
    std::unique_ptr<PersonalBoardApi> _api_per;
    std::unique_ptr<UserApi> _api_user;
};
