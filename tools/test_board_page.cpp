#include "../sql/Board_page.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace board_page;
    auto env = [](const char *name) {
        auto value = std::getenv(name);
        if (!value) throw std::runtime_error(std::string("missing test environment: ") + name);
        return std::string(value);
    };
    MysqlPool pool(env("PAGINATION_DB_HOST"), env("PAGINATION_DB_USER"),
        env("PAGINATION_DB_PASSWORD"), env("PAGINATION_DB_NAME"), 3306, 1);
    // Read-only smoke check against the actual schema before shadowing the table.
    for (auto sort : {"newest", "title", "difficulty"}) {
        auto response = respond(pool, std::nullopt, Json({{"sort", sort}}).dump());
        assert(response._status_code == 200);
        assert(Json::parse(response._body)["items"].size() <= 15);
    }
    auto sql = [&](const std::string &text) {
        auto connection = pool.acquire(std::chrono::milliseconds(1000));
        assert(connection);
        if (mysql_query(connection.get(), text.c_str())) {
            std::cerr << mysql_error(connection.get()) << '\n';
            throw std::runtime_error("fixture statement failed");
        }
    };
    // Session-local temporary table shadows board; real user data is never modified.
    sql("CREATE TEMPORARY TABLE board (id BIGINT PRIMARY KEY, author_id BIGINT NULL, "
        "title VARCHAR(200), category VARCHAR(200), difficulty VARCHAR(20), "
        "description TEXT, code LONGTEXT, language VARCHAR(40)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    for (int id = 1; id <= 35; ++id) {
        const auto category = id % 2 ? "图论 / 最短路" : "数论";
        const auto difficulty = id % 2 ? "简单" : "困难";
        const auto code = id == 1 ? "UNIQUE_ÉCHO 100% a_b" : "code";
        auto connection = pool.acquire(std::chrono::milliseconds(1000));
        Stmt stmt(connection.get());
        std::vector<std::string> params = {std::to_string(id), "模板" + std::to_string(id),
            category, difficulty, "description", code, "C++"};
        execute(stmt, "INSERT INTO board VALUES (?,NULL,?,?,?,?,?,?)", params);
    }
    sql("INSERT INTO board VALUES (100,7,'私有A','图论','简单','private','secret','C++'),"
        "(101,8,'私有B','图论','简单','private','secret','C++')");
    auto query = [&](const Json &options, std::optional<long long> user = std::nullopt) {
        auto response = respond(pool, user, options.dump());
        assert(response._status_code == 200);
        return Json::parse(response._body);
    };
    auto page = query({{"page", 2}});
    assert(page["total"] == 35 && page["items"].size() == 15);
    assert(page["items"][0]["id"] == 20 && page["items"][14]["id"] == 6);
    page = query({{"page", 99}});
    assert(page["page"] == 3 && page["items"].size() == 5);
    page = query({{"keyword", "unique_écho"}});
    assert(page["total"] == 1 && page["items"][0]["id"] == 1);
    assert(page["allTotal"] == 35 && page["categories"].size() == 2);
    for (auto literal : {"%", "_"}) {
        page = query({{"keyword", literal}});
        assert(page["total"] == 1 && page["items"][0]["id"] == 1);
    }
    assert(query({{"keyword", "unique_echo"}})["total"] == 0);
    assert(query({{"keyword", "' OR 1=1 --"}})["total"] == 0);
    assert(query({{"keyword", "secret"}})["total"] == 0);
    page = query({{"keyword", "secret"}}, 7);
    assert(page["total"] == 1 && page["items"][0]["id"] == 100 && page["allTotal"] == 1);
    page = query({{"page", 1}}, 8);
    assert(page["items"][0]["id"] == 101);
    page = query({{"page", 1}}, 9);
    assert(page["total"] == 0 && page["items"].empty());
    page = query({{"category", "图论"}, {"page", 2}});
    assert(page["total"] == 18 && page["items"].size() == 3);
    page = query({{"keyword", "模板"}, {"page", 2}});
    assert(page["total"] == 35 && page["items"].size() == 15);
    page = query({{"category", "数学与数论"}, {"exactCategory", "数论"}});
    assert(page["total"] == 17);
    page = query({{"language", "Python"}});
    assert(page["items"].empty() && page["totalPages"] == 1 && page["page"] == 1);
    page = query({{"difficulty", "困难"}, {"sort", "difficulty"}});
    assert(page["total"] == 17 && page["items"][0]["id"] == 34);
    assert(category_matches(" DP—区间 ", "动态规划"));
    assert(category_matches("其他：杂项", "综合与其他算法"));
    sql("UPDATE board SET title = '阿' WHERE id = 34");
    sql("UPDATE board SET title = '张' WHERE id = 35");
    page = query({{"sort", "title"}});
    assert(page["items"][0]["title"] == "阿");
    for (auto body : {"{", "[]", "{\"page\":0}", "{\"pageSize\":101}",
                     "{\"page\":1.5}", "{\"keyword\":3}", "{\"sort\":\"invalid\"}"})
        assert(respond(pool, std::nullopt, body)._status_code == 400);
    // Content SQL must be bounded even for searches and non-default sorting.
    Options options({{"page", 2}, {"keyword", "code"}, {"sort", "title"}});
    auto filter = make_filter(7, options, {"图论"});
    const auto statement = page_sql(filter, options, 2);
    assert(statement.find("LIMIT 15 OFFSET 15") != std::string::npos);
    assert(statement.find("author_id = ?") != std::string::npos);
    assert(statement.find("INSTR") != std::string::npos);
    sql("DELETE FROM board WHERE author_id IS NULL AND id <= 5");
    page = query({{"page", 3}});
    assert(page["page"] == 2 && page["items"].size() == 15);
    sql("DELETE FROM board WHERE author_id IS NULL");
    page = query({{"page", 8}});
    assert(page["page"] == 1 && page["items"].empty() && page["allTotal"] == 0);
    std::cout << "Database pagination tests passed (temporary table only)\n";
}
