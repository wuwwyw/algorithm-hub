#pragma once
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include "../net/Http_response.hpp"
#include "Stmt.hpp"
#include "Passwd.hpp"
#include "MysqlPool.hpp"
#include "Pdf_exporter.hpp"
#include "Board_request.hpp"
#include "../redis/Session_manager.hpp"


using json = nlohmann::json;

class ApiHandle {
public:
    ApiHandle(MysqlPool &mysql_pool) : _mysql_pool(mysql_pool) {}
    virtual ~ApiHandle() = default;

    Http_response json_response(int status_code, const std::string& status_text, const std::string& body) {
        Http_response response(status_code, status_text, body);
        response.add_header("Content-Type", "application/json");
        return response;
    }
protected:
    struct OperResult {
        bool success;
        my_ulonglong affected_rows;
        my_ulonglong insert_id;
        unsigned int error_code;
    };

    template <typename BindFunc>
    OperResult oper_sql(const std::string &sql, BindFunc &&bind_func) {
        auto conn = _mysql_pool.acquire(std::chrono::milliseconds(1000));
        if (!conn) {
            return {false, 0, 0, 0};
        }

        Stmt stmt(conn->get());
        if (!stmt.prepare(sql) || !bind_func(stmt) || !stmt.execute()) return {false, 0, 0, stmt.error_code()};
        return {
            true,
            mysql_stmt_affected_rows(stmt.get()),
            mysql_insert_id(conn->get()),
            0
        };
    }

    template <typename BindFunc, typename ResultFunc>
    FetchResult query_sql(const std::string& sql, BindFunc&& bind_func, ResultFunc&& result_func) {
        auto conn = _mysql_pool.acquire(std::chrono::milliseconds(1000));
        if (!conn) {
            return FetchResult::ERROR;
        }
        Stmt stmt(conn->get());

        if (!stmt.prepare(sql)) {
            return FetchResult::ERROR;
        }
        if (!bind_func(stmt)) {
            return FetchResult::ERROR;
        }
        if (!stmt.execute()) {
            return FetchResult::ERROR;
        }

        return result_func(stmt);
    }

    Http_response query_boards(std::optional<long long> user_id, std::optional<long long> board_id) {
        std::string sql =
            "SELECT id, title, category, difficulty, "
            "description, code, language "
            "FROM board WHERE ";

        std::vector<long long> params;

        if (user_id.has_value()) {
            sql += "author_id = ?";
            params.push_back(*user_id);
        } else {
            sql += "author_id IS NULL";
        }

        if (board_id.has_value()) {
            sql += " AND id = ?";
            params.push_back(*board_id);
        }

        sql += " ORDER BY id DESC";

        json boards = json::array();
        long long id{};
        std::string title;
        std::string category;
        std::string difficulty;
        std::string description;
        std::string code;
        std::string language;

        FetchResult query_result = query_sql(
            sql,
            [&](Stmt& stmt) -> bool {
                return stmt.bind_many(params);
            },
            [&](Stmt& stmt) -> FetchResult {
                if (!stmt.bind_result(
                    id,
                    title,
                    category,
                    difficulty,
                    description,
                    code,
                    language
                )) {
                    return FetchResult::ERROR;
                }

                while (true) {
                    FetchResult fetch_result = stmt.fetch();
                    if (fetch_result == FetchResult::NO_DATA) {
                        return FetchResult::SUCCESS;
                    }
                    if (fetch_result == FetchResult::ERROR) {
                        return FetchResult::ERROR;
                    }

                    boards.push_back({
                        {"id", id},
                        {"title", title},
                        {"category", category},
                        {"difficulty", difficulty},
                        {"description", description},
                        {"code", code},
                        {"language", language}
                    });
                }
            }
        );

        if (query_result == FetchResult::ERROR) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database query failed"})"
            );
        }

        if (board_id.has_value()) {
            if (boards.empty()) {
                return json_response(
                    404,
                    "Not Found",
                    R"({"code":404,"message":"board not found"})"
                );
            }

            return json_response(
                200,
                "OK",
                boards[0].dump()
            );
        }

        return json_response(
            200,
            "OK",
            boards.dump()
        );
    }

protected:
    MysqlPool &_mysql_pool;
};

class PublicBoardsApi : public ApiHandle {
public:
    explicit PublicBoardsApi(MysqlPool &mysql_pool) : ApiHandle(mysql_pool) {}

    Http_response get_boards() {
        return query_boards(std::nullopt, std::nullopt);
    }

    Http_response get_board(long long board_id) {
        return query_boards(std::nullopt, board_id);
    }
};


class PersonalBoardApi : public ApiHandle {
public:
    explicit PersonalBoardApi(MysqlPool &mysql_pool) : ApiHandle(mysql_pool) {}

    Http_response get_boards(long long user_id) {
        return query_boards(user_id, std::nullopt);
    }

    Http_response get_board(long long user_id, long long board_id) {
        return query_boards(user_id, board_id);
    }

    Http_response create_board(long long user_id, const std::string &body) {
        json data;
        if (!parse_board_request(body, data)) {
            json error = {
                {"code", 400},
                {"message", "missing or invalid field"}
            };
            return json_response(
                400,
                "Bad Request",
                error.dump()
            );
        }

        const std::string title       = data["title"].get<std::string>();
        const std::string category    = data["category"].get<std::string>();
        const std::string difficulty  = data["difficulty"].get<std::string>();
        const std::string description = data["description"].get<std::string>();
        const std::string code        = data["code"].get<std::string>();
        const std::string language    = data["language"].get<std::string>();

        const std::string sql = R"(
            INSERT INTO board
            (title, category, difficulty, description, code, language, author_id)
            VALUE (?, ?, ?, ?, ?, ?, ?)
        )";

        OperResult oper = oper_sql(sql, [&] (Stmt &stmt) -> bool {
            return stmt.bind(title, category, difficulty, description, code, language, user_id);
        });

        if (!oper.success) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database operation failed"})"
            );
        }

        json result = {
            {"id", oper.insert_id}
        };
        return json_response(201, "Created", result.dump());
    }

    Http_response export_pdf(long long user_id, const std::string &body) {
        Pdf_exporter pdf_exter;
        json data;
        if (!parse_board_request(body, data, true)) {
            json error = {
                {"code", 400},
                {"message", "missing or invalid field"}
            };
            return json_response(
                400,
                "Bad Request",
                error.dump()
            );
        }

        std::vector<long long> board_ids = data["board_ids"].get<std::vector<long long>>();
        std::vector<long long> params;
        params.reserve(board_ids.size() + 1);
        params.push_back(user_id);
        params.insert(params.end(), board_ids.begin(), board_ids.end());

        const std::string sql = 
            "SELECT id, title, category, difficulty, "
            "description, code, language "
            "FROM board "
            "WHERE author_id = ? "
                "AND id IN (" +
                Stmt::make_placeholders(data["board_ids"].size()) +
            ")";

        std::vector<Board> boards;
        std::unordered_set <std::string> actual_categories;
        long long found_id{};
        std::string title;
        std::string category;
        std::string difficulty;
        std::string description;
        std::string code;
        std::string language;

        FetchResult result = query_sql(
            sql, 
            [&] (Stmt &stmt) -> bool {
                return stmt.bind_many(params);
            },
            [&] (Stmt &stmt) -> FetchResult {
                if (!stmt.bind_result(
                    found_id,
                    title,
                    category,
                    difficulty,
                    description,
                    code,
                    language
                )) {
                    return FetchResult::ERROR;
                }

                FetchResult fetch_result;
                while ((fetch_result = stmt.fetch()) != FetchResult::NO_DATA) {
                    if (fetch_result == FetchResult::ERROR) return fetch_result;
                    boards.push_back({
                        found_id,
                        title,
                        category,
                        difficulty,
                        description,
                        code,
                        language
                    });
                    auto real_category = pdf_exter.cate_to_real(category);
                    if (!real_category) {
                        return FetchResult::ERROR;
                    }
                    actual_categories.insert(*real_category);
                }

                return FetchResult::SUCCESS;
            }
        );

        if (result == FetchResult::ERROR) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database operation failed"})"
            );
        }
        if (boards.size() != data["board_ids"].size()) {
            return json_response(
                404,
                "Not Found",
                R"({"code":404,"message":"export operation failed"})"
            );
        }

        export_option opt;
        opt.cover = data["option"]["cover"];
        opt.table_of_contents = data["option"]["table_of_contents"];
        opt.bookmarks = data["option"]["bookmarks"];
        opt.new_page_per_category = data["option"]["new_page_per_category"];

        for (auto it = data["category"].begin(); it != data["category"].end(); it++) {
            if (actual_categories.find(*it) == actual_categories.end()) {
                return json_response(
                    400,
                    "Bad Request",
                    R"({"code":400,"message":"missing or invalid field"})"
                );
            }
            actual_categories.erase(*it);
        }
        if (actual_categories.size() != 0) {
            return json_response(
                400,
                "Bad Request",
                R"({"code":400,"message":"missing or invalid field"})"
            );
        }
        
        Pdf_result pdf_result = pdf_exter.generate(boards, data["category"], opt);
        if (!pdf_result.success) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"PDF generation failed"})"
            );
        }

        Http_response response(200, "OK", pdf_result.bytes);
        response.add_header("Content-Type", "application/pdf");
        response.add_header(
            "Content-Disposition",
            "attachment; filename=\"algorithm-hub-boards.pdf\""
        );
        response.add_header("Cache-Control", "no-store");
        return response;
    }

    Http_response join_board(long long user_id, long long board_id) {
        const std::string sql = R"(
            INSERT INTO board
            (title, category, difficulty, description, code, language, author_id, source_board_id)
            SELECT 
                title, category, difficulty, description, code, language, ?, id
            FROM board
            WHERE id = ?
            AND author_id IS NULL
        )";

        OperResult oper = oper_sql(sql, [&] (Stmt &stmt) -> bool {
            return stmt.bind(user_id, board_id);
        });

        if (!oper.success) {
            if (oper.error_code == 1062) {
                return json_response(
                    409,
                    "Conflict",
                    R"({"code":409,"message":"board already joined"})"
                );
            }
            
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database operation failed"})"
            );
        }

        if (oper.affected_rows == 0) {
            return json_response(
                404,
                "Not Found",
                R"({"code":404,"message":"public board not found"})"
            );
        }

        json result = {
            {"id", oper.insert_id}
        };

        return json_response(
            201,
            "create",
            result.dump()
        );
    }

    Http_response update_board(long long user_id, long long board_id, const std::string &body) {
        json data;
        if (!parse_board_request(body, data)) {
            json error = {
                {"code", 400},
                {"message", "missing or invalid field"}
            };
            return json_response(
                400,
                "Bad Request",
                error.dump()
            );
        }

        try {
            if (!board_exists_for_user(user_id, board_id)) {
                return json_response(
                    404,
                    "Not Found",
                    R"({"code":404,"message":"board not found"})"
                );
            }
        } catch (...) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database query failed"})"
            );
        }

        const std::string title       = data["title"].get<std::string>();
        const std::string category    = data["category"].get<std::string>();
        const std::string difficulty  = data["difficulty"].get<std::string>();
        const std::string description = data["description"].get<std::string>();
        const std::string code        = data["code"].get<std::string>();
        const std::string language    = data["language"].get<std::string>();

        const std::string sql = R"(
            UPDATE board
            SET title = ?, 
                category = ?,
                difficulty = ?,
                description = ?,
                code = ?,
                language = ?,
                updated_at = CURRENT_TIMESTAMP
                WHERE id = ?
                AND author_id = ?
        )";

        OperResult oper = oper_sql(sql, [&] (Stmt &stmt) -> bool {
            return stmt.bind(title, category, difficulty, description, code, language, board_id, user_id);
        });

        if (!oper.success) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database operation failed"})"
            );
        }

        json result = {
            {"affected_rows", oper.affected_rows}
        };
        return json_response(200, "OK", result.dump());
    }

    Http_response delete_board(long long user_id, long long board_id) {
        const std::string sql = R"(
            DELETE FROM board
            WHERE id = ?
            AND author_id = ?
        )";

        OperResult oper = oper_sql(sql, [&] (Stmt &stmt) -> bool {
            return stmt.bind(board_id, user_id);
        });

        if (!oper.success) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database operation failed"})"
            );
        }

        if (oper.affected_rows == 0) {
            return json_response(
                404,
                "Not Found",
                R"({"code":404,"message":"board not found"})"
            );
        }

        json result = {
            {"affected_rows", oper.affected_rows}
        };
        return json_response(200, "OK", result.dump());
    }

private:
    bool board_exists_for_user(long long user_id, long long board_id) {
        const std::string sql = R"(
            SELECT id
            FROM board
            WHERE id = ?
            AND author_id = ?
        )";

        long long id{};

        FetchResult result = query_sql(
            sql,
            [&](Stmt &stmt) -> bool {
                return stmt.bind(board_id, user_id);
            },
            [&](Stmt &stmt) -> FetchResult {
                if (!stmt.bind_result(id)) {
                    return FetchResult::ERROR;
                }

                return stmt.fetch();
            }
        );

        if (result == FetchResult::ERROR) {
            throw std::runtime_error("board ownership query failed");
        }

        return result == FetchResult::SUCCESS;
    }
};

class UserApi : public ApiHandle {
public:
    explicit UserApi(MysqlPool &mysql_pool, Session_manager &session) : ApiHandle(mysql_pool), _session(session) {}

    Http_response register_user(const std::string &body) {
        std::string username;
        std::string password;
        if (!parse_user(body, username, password)) {
            return json_response(
                400, 
                "Bad Request", 
                R"({"code":400,"message":"invalid username or password"})"
            );
        }

        std::string password_hash;
        try {
            password_hash = Password::hash(password);
        } catch (...) {
            return json_response(
                500, 
                "Internal Server Error", 
                R"({"code":500,"message":"password hash failed"})"
            );
        }

        const std::string sql = R"(INSERT INTO `user` (username, password_hash) VALUES (?, ?))";
        
        OperResult oper = oper_sql(sql, [&] (Stmt &stmt) -> bool {
            return stmt.bind(username, password_hash);
        });
        if (!oper.success) {
            if (oper.error_code == 1062) {
                return json_response(
                    409,
                    "Conflict",
                    R"({"code":409,"message":"username already exists"})"
                );
            }

            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database operation failed"})"
            );
        }

        json result = {
            {"id", oper.insert_id}, 
            {"username", username}
        };
        return json_response(
            201, 
            "Created", 
            result.dump()
        );
    }

    Http_response login_user(const std::string &body) {
        std::string username, password;
        if (!parse_user(body, username, password)) {
                return json_response(
                    400, 
                    "Bad Request", 
                    R"({"code":400,"message":"invalid username or password"})"
            );
        }

        const std::string sql = R"(SELECT id, password_hash FROM `user` WHERE username = ?)";

        long long user_id{};
        std::string password_hash;
        FetchResult success = query_sql(
            sql, 
            [&](Stmt &stmt) -> bool {
                return stmt.bind(username);
            },
            [&](Stmt &stmt) -> FetchResult {
                if (!stmt.bind_result(user_id, password_hash)) {
                    return FetchResult::ERROR;
                }
                return stmt.fetch();
            });
        
        if (success == FetchResult::ERROR) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"database operation failed"})"
            );
        }
        
        if (success == FetchResult::NO_DATA || !Password::verify(password, password_hash)) {
            return json_response(
                401,
                "Unauthorized",
                R"({"code":401,"message":"invalid username or password"})"
            );
        }

        auto session_id = _session.create(user_id);
        if (!session_id) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"session create failed"})"
            );
        }

        json result = {
            {"id", user_id},
            {"username", username}
        };
        Http_response response = json_response(
            200,
            "OK",
            result.dump()
        );
        response.add_header("Set-Cookie", _session.make_Cookie(*session_id));
        return response;
    }

    Http_response logout_user(const Headers &headers) {
        if (!_session.destroy(headers)) {
            return json_response(
                500,
                "Internal Server Error",
                R"({"code":500,"message":"logout failed"})"
            );
        }

        Http_response response = json_response(
            200,
            "OK",
            R"({"code":200,"message":"logout success"})"
        );
        response.add_header("Set-Cookie", _session.expire_cookie());
        return response;
    }

    Http_response me(const Headers &headers) {
        auto user_id = _session.get_user_id(headers);
        if (!user_id) {
            return json_response(
                401,
                "Unauthorized",
                R"({"code":401,"message":"not logged in"})"
            );
        }
        json result = {
            {"id", *user_id}
        };
        return json_response(
            200,
            "OK",
            result.dump()
        );
    }

private:
    bool parse_user(const std::string& body, std::string& username, std::string& password) {
        json data;
        try {
            data = json::parse(body);
        } catch (...) {
            return false;
        }

        if (!data.contains("username") || !data.contains("password") || !data["username"].is_string() || !data["password"].is_string()) {
            return false;
        }

        username = data["username"].get<std::string>();
        password = data["password"].get<std::string>();

        if (username.empty() || username.size() > 64 || password.size() < 6 || password.size() > 128) {
            return false;
        }

        return true;
    }


private:
    Session_manager &_session;
};
