#pragma once
#include <climits>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

inline bool parse_board_request(const std::string &body, nlohmann::json &data, bool exporting = false) {
    try {
        data = nlohmann::json::parse(body);
    } catch (...) {
        return false;
    }

    if (!data.is_object()) return false;
    if (exporting) {
        if (!data.contains("board_ids")) return false;
        if (!data["board_ids"].is_array() || data["board_ids"].size() == 0) return false;
        for (auto id : data["board_ids"]) {
            if (!id.is_number_integer() || id <= 0 ||
                (id.is_number_unsigned() && id.get<unsigned long long>() > static_cast<unsigned long long>(LLONG_MAX))) return false;
        }

        if (!data.contains("option") || !data["option"].is_object()) return false;
        const auto& options = data["option"];
        for (auto it = options.begin(); it != options.end(); it++) {
            std::string flag = it.key();
            if (
                flag != "cover" && 
                flag != "table_of_contents" && 
                flag != "bookmarks" && 
                flag != "new_page_per_category"
            ) {
                return false;
            }

            if (!(*it).is_boolean()) return false;
        }

        if (
            !options.contains("cover") ||
            !options.contains("table_of_contents") ||
            !options.contains("bookmarks") ||
            !options.contains("new_page_per_category")
        ) {
            return false;
        }

        std::vector<std::string> category = {
            "basic",
            "data_structure",
            "graph",
            "string",
            "math",
            "dp",
            "greedy",
            "geometry",
            "randomized",
            "other"
        };

        if (!data.contains("category") || !data["category"].is_array() || data["category"].size() == 0) return false;
        for (const auto &entry : data["category"]) {
            if (!entry.is_string()) return false;
            const auto cate = entry.get<std::string>();
            bool flag = false;
            for (const std::string &rule_cate : category) {
                if (cate == rule_cate) {
                    flag = true;
                    break;
                }
            }
            if (!flag) return false;
        }
    } else {
        std::vector<std::string> required = {
            "title",
            "category",
            "difficulty",
            "description",
            "code",
            "language"
        };

        for (const std::string &field: required) {
            if (!data.contains(field) || !data[field].is_string()) {
                return false;
            }
        }
    }

    return true;
}

