#include "../sql/Board_request.hpp"
#include <cassert>
#include <iostream>
int main() {
    nlohmann::json parsed;
    const auto board = nlohmann::json{{"title", "test"}, {"category", "数学"},
        {"difficulty", "简单"}, {"description", ""}, {"code", "int main() {}"}, {"language", "C++"}};
    auto request = nlohmann::json{{"board_ids", {1}}, {"category", {"math"}},
        {"option", {{"cover", true}, {"table_of_contents", false}, {"bookmarks", false}, {"new_page_per_category", false}}}};
    assert(parse_board_request(board.dump(), parsed));
    assert(parse_board_request(request.dump(), parsed, true));
    assert(!parse_board_request(board.dump(), parsed, true));
    assert(!parse_board_request(request.dump(), parsed));
    for (auto bad : {nlohmann::json(42), nlohmann::json(nullptr), nlohmann::json::array()})
        assert(!parse_board_request(bad.dump(), parsed, true));
    auto invalid = request;
    invalid["category"] = {42};
    assert(!parse_board_request(invalid.dump(), parsed, true));
    for (auto id : {nlohmann::json(-1), nlohmann::json(0), nlohmann::json(18446744073709551615ULL)}) {
        invalid = request; invalid["board_ids"] = {id};
        assert(!parse_board_request(invalid.dump(), parsed, true));
    }
    std::cout << "PASS: save/export validation, category types, integer bounds\n";
}
