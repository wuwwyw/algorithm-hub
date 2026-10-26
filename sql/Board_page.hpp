#pragma once
#include <algorithm>
#include <memory>
#include <optional>
#include "MysqlPool.hpp"
#include "Stmt.hpp"
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <unicode/coll.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include "../net/Http_response.hpp"

// Search the complete authorized collection, but fetch board contents only with LIMIT/OFFSET.
namespace board_page {
using Json = nlohmann::json;

inline std::string lower(const std::string &text, bool remove_spaces = false) {
    auto value = icu::UnicodeString::fromUTF8(text);
    value.toLower(icu::Locale::getRoot());
    icu::UnicodeString normalized;
    for (int32_t i = 0; i < value.length();) {
        UChar32 ch = value.char32At(i);
        i += U16_LENGTH(ch);
        if (!remove_spaces || (!u_isUWhiteSpace(ch) && ch != 0xfeff)) normalized.append(ch);
    }
    std::string result;
    normalized.toUTF8String(result);
    return result;
}

inline bool category_matches(const std::string &value, const std::string &category) {
    static const std::unordered_map<std::string, std::string> aliases = {
        {"数论", "数学与数论"}, {"数学", "数学与数论"}, {"基础", "基础算法"},
        {"贪心", "贪心与构造"}, {"构造", "贪心与构造"}, {"几何", "计算几何"},
        {"dp", "动态规划"}, {"随机", "随机化与启发式"}, {"随机化", "随机化与启发式"},
        {"启发式", "随机化与启发式"}, {"综合", "综合与其他算法"}, {"其他", "综合与其他算法"}
    };
    const auto separators = icu::UnicodeString::fromUTF8("/／>＞|｜·•-—:：,，");
    const auto source = icu::UnicodeString::fromUTF8(value);
    const auto target = lower(category, true);
    icu::UnicodeString part;
    auto matches = [&]() {
        std::string text;
        part.toUTF8String(text);
        auto key = lower(text, true);
        auto alias = aliases.find(key);
        return (alias == aliases.end() ? key : alias->second) == target;
    };
    for (int32_t i = 0; i < source.length();) {
        auto ch = source.char32At(i);
        i += U16_LENGTH(ch);
        if (separators.indexOf(ch) >= 0) {
            if (matches()) return true;
            part.remove();
        } else part.append(ch);
    }
    return matches();
}


struct Options {
    long long page = 1;
    int size = 15;
    std::string keyword, category, exact, language, difficulty, sort;
    explicit Options(const Json &options) {
        if (!options.is_object()) throw std::invalid_argument("invalid page options");
        auto integer = [&](const char *key, int fallback, int maximum) {
            if (!options.contains(key)) return fallback;
            const auto &value = options.at(key);
            if (!value.is_number_integer() || value < 1 || value > maximum)
                throw std::invalid_argument("invalid page or pageSize");
            return value.get<int>();
        };
        page = integer("page", 1, 1000000000);
        size = integer("pageSize", 15, 100);
        keyword = lower(options.value("keyword", std::string{}));
        category = options.value("category", std::string{});
        exact = options.value("exactCategory", std::string{});
        language = options.value("language", std::string{});
        difficulty = options.value("difficulty", std::string{});
        sort = options.value("sort", std::string{"newest"});
        if (sort != "newest" && sort != "title" && sort != "difficulty")
            throw std::invalid_argument("invalid sort");
    }
};

// Keep metadata, matching count and page contents on the same database snapshot.
class ReadSnapshot {
    MYSQL *connection;
public:
    explicit ReadSnapshot(MYSQL *conn) : connection(conn) {
        if (mysql_query(conn, "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ") ||
            mysql_query(conn, "START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY"))
            throw std::runtime_error("cannot begin read snapshot");
    }
    ~ReadSnapshot() { mysql_rollback(connection); }
    ReadSnapshot(const ReadSnapshot &) = delete;
    ReadSnapshot &operator=(const ReadSnapshot &) = delete;
};

inline void execute(Stmt &stmt, const std::string &sql, std::vector<std::string> &params) {
    if (!stmt.prepare(sql) || !stmt.bind_many(params) || !stmt.execute())
        throw std::runtime_error("board page query failed");
}

struct Filter {
    std::string where;
    std::vector<std::string> params;
};

inline Filter make_filter(std::optional<long long> user_id, const Options &options,
                          const std::set<std::string> &categories) {
    Filter filter{user_id ? "author_id = ?" : "author_id IS NULL", {}};
    if (user_id) filter.params.push_back(std::to_string(*user_id));
    auto exact = [&](const char *field, const std::string &value) {
        if (value.empty()) return;
        filter.where += " AND CAST(" + std::string(field) + " AS BINARY) = CAST(? AS BINARY)";
        filter.params.push_back(value);
    };
    exact("category", options.exact);
    exact("language", options.language);
    exact("difficulty", options.difficulty);
    if (!options.category.empty()) {
        std::vector<std::string> matching;
        for (const auto &category : categories)
            if (category_matches(category, options.category)) matching.push_back(category);
        if (matching.empty()) filter.where += " AND 1 = 0";
        else {
            filter.where += " AND CAST(category AS BINARY) IN (";
            for (size_t i = 0; i < matching.size(); ++i) {
                if (i) filter.where += ',';
                filter.where += "CAST(? AS BINARY)";
                filter.params.push_back(matching[i]);
            }
            filter.where += ')';
        }
    }
    if (!options.keyword.empty()) {
        // INSTR treats %, _ and backslash as literal characters, like JS includes().
        // Binary comparison after LOWER preserves accent-sensitive substring matching.
        filter.where += " AND INSTR(CAST(LOWER(CONCAT("
            "COALESCE(title,''),' ',COALESCE(category,''),' ',COALESCE(difficulty,''),' ',"
            "COALESCE(description,''),' ',COALESCE(language,''),' ',COALESCE(code,''))) AS BINARY),"
            "CAST(? AS BINARY)) > 0";
        filter.params.push_back(options.keyword);
    }
    return filter;
}

inline std::string page_sql(const Filter &filter, const Options &options, long long page) {
    std::string order = "id DESC";
    if (options.sort == "title")
        order = "COALESCE(title,'') COLLATE utf8mb4_zh_0900_as_cs ASC, id DESC";
    else if (options.sort == "difficulty")
        order = "CASE difficulty WHEN '简单' THEN 1 WHEN '中等' THEN 2 WHEN '困难' THEN 3 ELSE 99 END, id DESC";
    // Only validated integers are formatted; every user-supplied string is bound.
    return "SELECT id, title, category, difficulty, description, code, language FROM board WHERE " +
        filter.where + " ORDER BY " + order + " LIMIT " + std::to_string(options.size) +
        " OFFSET " + std::to_string((page - 1) * options.size);
}

inline Json query_page(MYSQL *conn, std::optional<long long> user_id, const Options &options) {
    ReadSnapshot snapshot(conn);
    const std::string scope = user_id ? "author_id = ?" : "author_id IS NULL";
    std::vector<std::string> scope_params;
    if (user_id) scope_params.push_back(std::to_string(*user_id));
    std::set<std::string> categories, languages;
    long long all_total = 0;
    {
        // Aggregate metadata only: never read all titles, descriptions or code for pagination.
        Stmt stmt(conn);
        execute(stmt, "SELECT CAST(category AS BINARY), CAST(language AS BINARY), COUNT(*) FROM board WHERE " +
            scope + " GROUP BY CAST(category AS BINARY), CAST(language AS BINARY)", scope_params);
        std::string category, language;
        long long count = 0;
        if (!stmt.bind_result(category, language, count)) throw std::runtime_error("metadata bind failed");
        for (;;) {
            auto result = stmt.fetch();
            if (result == FetchResult::NO_DATA) break;
            if (result == FetchResult::ERROR) throw std::runtime_error("metadata fetch failed");
            all_total += count;
            if (!category.empty()) categories.insert(category);
            if (!language.empty()) languages.insert(language);
        }
    }
    auto filter = make_filter(user_id, options, categories);
    long long total = 0;
    {
        Stmt stmt(conn);
        execute(stmt, "SELECT COUNT(*) FROM board WHERE " + filter.where, filter.params);
        if (!stmt.bind_result(total) || stmt.fetch() != FetchResult::SUCCESS)
            throw std::runtime_error("count query failed");
    }
    const long long pages = std::max(1LL, (total + options.size - 1) / options.size);
    const long long page = std::min(options.page, pages);
    Json items = Json::array();
    {
        Stmt stmt(conn);
        execute(stmt, page_sql(filter, options, page), filter.params);
        long long id = 0;
        std::string title, category, difficulty, description, code, language;
        if (!stmt.bind_result(id, title, category, difficulty, description, code, language))
            throw std::runtime_error("page bind failed");
        for (;;) {
            auto result = stmt.fetch();
            if (result == FetchResult::NO_DATA) break;
            if (result == FetchResult::ERROR) throw std::runtime_error("page fetch failed");
            items.push_back({{"id", id}, {"title", title}, {"category", category},
                {"difficulty", difficulty}, {"description", description}, {"code", code}, {"language", language}});
        }
    }
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Collator> collator(icu::Collator::createInstance(icu::Locale("zh_CN"), status));
    if (U_FAILURE(status) || !collator) throw std::runtime_error("cannot create Chinese collator");
    std::vector<std::string> category_list(categories.begin(), categories.end());
    std::sort(category_list.begin(), category_list.end(), [&](const std::string &a, const std::string &b) {
        return collator->compare(icu::UnicodeString::fromUTF8(a), icu::UnicodeString::fromUTF8(b)) == icu::Collator::LESS;
    });
    return {{"items", items}, {"page", page}, {"pageSize", options.size}, {"total", total},
        {"totalPages", pages}, {"allTotal", all_total}, {"categories", category_list}, {"languageCount", languages.size()}};
}

inline Http_response respond(MysqlPool &pool, std::optional<long long> user_id, const std::string &body) {
    auto response = [](int code, const std::string &reason, const Json &data) -> Http_response {
        Http_response result(code, reason, data.dump());
        result.add_header("Content-Type", "application/json");
        result.add_header("Cache-Control", "no-store");
        return result;
    };
    try {
        const Options options(Json::parse(body));
        auto connection = pool.acquire(std::chrono::milliseconds(1000));
        if (!connection) throw std::runtime_error("database unavailable");
        return response(200, "OK", query_page(connection.get(), user_id, options));
    } catch (const Json::exception &) {
        return response(400, "Bad Request", {{"message", "invalid pagination request"}});
    } catch (const std::invalid_argument &e) {
        return response(400, "Bad Request", {{"message", e.what()}});
    } catch (const std::exception &) {
        return response(500, "Internal Server Error", {{"message", "pagination query failed"}});
    }
}
} // namespace board_page
