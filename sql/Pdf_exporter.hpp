#pragma once

#include <algorithm>
#include <fstream>
#include <hpdf.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef ALGORITHM_HUB_PDF_REGULAR_FONT_PATH
#define ALGORITHM_HUB_PDF_REGULAR_FONT_PATH ""
#endif

#ifndef ALGORITHM_HUB_PDF_BOLD_FONT_PATH
#define ALGORITHM_HUB_PDF_BOLD_FONT_PATH ""
#endif

#ifndef ALGORITHM_HUB_PDF_MONO_FONT_PATH
#define ALGORITHM_HUB_PDF_MONO_FONT_PATH ""
#endif

#ifndef ALGORITHM_HUB_PDF_LOGO_PATH
#define ALGORITHM_HUB_PDF_LOGO_PATH ""
#endif

//AI完成

using json = nlohmann::json;

struct export_option {
    bool cover;
    bool table_of_contents;
    bool bookmarks;
    bool new_page_per_category;
};

struct Board {
    long long id;
    std::string title;
    std::string category;
    std::string difficulty;
    std::string description;
    std::string code;
    std::string language;
};

struct Pdf_result {
    bool success;
    std::string bytes;
    std::string error;
};

class Pdf_exporter {
public:
    Pdf_result generate(
        std::vector<Board>& boards,
        const json& category_order,
        const export_option& opt
    ) {
        boards_sort(category_order, boards);

        const std::string regular_font_path = ALGORITHM_HUB_PDF_REGULAR_FONT_PATH;
        const std::string bold_font_path = ALGORITHM_HUB_PDF_BOLD_FONT_PATH;
        const std::string mono_font_path = ALGORITHM_HUB_PDF_MONO_FONT_PATH;
        std::ifstream regular_file(regular_font_path, std::ios::binary);
        if (regular_font_path.empty() || !regular_file.good()) {
            return {false, {}, "regular Chinese PDF font is not configured"};
        }

        HPDF_Doc pdf = HPDF_New(nullptr, nullptr);
        if (pdf == nullptr) {
            return {false, {}, "could not create PDF document"};
        }

        auto fail = [&](const std::string& error) -> Pdf_result {
            HPDF_Free(pdf);
            return {false, {}, error};
        };

        if (HPDF_UseUTFEncodings(pdf) != HPDF_OK ||
            HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL) != HPDF_OK ||
            HPDF_SetInfoAttr(pdf, HPDF_INFO_TITLE, "Algorithm Hub Board Collection") != HPDF_OK) {
            return fail("could not configure PDF document");
        }

        auto load_font = [&](const std::string& font_path) -> HPDF_Font {
            const char* font_name = HPDF_LoadTTFontFromFile(
                pdf, font_path.c_str(), HPDF_TRUE
            );
            return font_name == nullptr ? nullptr : HPDF_GetFont(pdf, font_name, "UTF-8");
        };

        HPDF_Font regular_font = load_font(regular_font_path);
        if (regular_font == nullptr) {
            return fail("could not load regular Chinese font");
        }
        HPDF_Font bold_font = bold_font_path.empty() ? regular_font : load_font(bold_font_path);
        if (bold_font == nullptr) {
            return fail("could not load bold Chinese font");
        }
        HPDF_Font mono_font = mono_font_path.empty() ? regular_font : load_font(mono_font_path);
        if (mono_font == nullptr) {
            return fail("could not load monospace code font");
        }

        HPDF_Image logo = nullptr;
        const std::string logo_path = ALGORITHM_HUB_PDF_LOGO_PATH;
        std::ifstream logo_file(logo_path, std::ios::binary);
        if (!logo_path.empty() && logo_file.good()) {
            logo = HPDF_LoadPngImageFromFile(pdf, logo_path.c_str());
            if (logo == nullptr) {
                return fail("could not load PDF cover logo");
            }
        }

        int page_number = 0;
        auto add_page = [&]() -> HPDF_Page {
            HPDF_Page page = HPDF_AddPage(pdf);
            if (page == nullptr ||
                HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT) != HPDF_OK) {
                return nullptr;
            }
            ++page_number;
            if (!draw_centered(
                    page,
                    regular_font,
                    8,
                    26,
                    "Algorithm Hub  ·  " + std::to_string(page_number),
                    0.42, 0.47, 0.55
                )) {
                return nullptr;
            }
            return page;
        };

        if (opt.cover) {
            HPDF_Page cover = add_page();
            const HPDF_REAL page_width = cover == nullptr ? 0 : HPDF_Page_GetWidth(cover);
            const HPDF_REAL page_height = cover == nullptr ? 0 : HPDF_Page_GetHeight(cover);
            std::unordered_set<std::string> categories;
            for (const Board& board : boards) {
                const auto category = cate_to_real(board.category);
                if (category) {
                    categories.insert(*category);
                }
            }
            if (cover == nullptr ||
                !fill_rect(cover, 0, 0, page_width, page_height, 0.96, 0.97, 0.99) ||
                !fill_rect(cover, 0, page_height - 180, page_width, 180, 0.08, 0.16, 0.30) ||
                (logo != nullptr && HPDF_Page_DrawImage(cover, logo, page_width / 2 - 74, 465, 148, 148) != HPDF_OK) ||
                !draw_centered(cover, bold_font, 30, page_height - 115, "算法板子合集", 1, 1, 1) ||
                !draw_centered(cover, regular_font, 13, page_height - 148, "Algorithm Hub · Board Collection", 0.80, 0.86, 0.96) ||
                !draw_centered(cover, bold_font, 17, 430, "精选算法与代码笔记", 0.08, 0.16, 0.30) ||
                !draw_centered(
                    cover,
                    regular_font,
                    13,
                    388,
                    "共 " + std::to_string(boards.size()) + " 块板子 · " +
                        std::to_string(categories.size()) + " 个分类",
                    0.28, 0.33, 0.42
                ) ||
                !draw_centered(cover, regular_font, 10, 92, "由 Algorithm Hub 生成", 0.42, 0.47, 0.55)) {
                return fail("could not render cover page");
            }
        }

        HPDF_Page toc_page = nullptr;
        if (opt.table_of_contents) {
            toc_page = add_page();
            if (toc_page == nullptr) {
                return fail("could not create table of contents page");
            }
        }

        std::vector<std::pair<std::string, int>> category_pages;
        HPDF_Page page = nullptr;
        HPDF_REAL y = 0;
        std::string previous_category;

        for (const Board& board : boards) {
            const auto real_category = cate_to_real(board.category);
            if (!real_category) {
                return fail("board contains an unknown category");
            }

            const bool new_category = *real_category != previous_category;
            if (page == nullptr ||
                (new_category && (opt.new_page_per_category || y < k_bottom_margin + 70))) {
                page = add_page();
                if (page == nullptr) {
                    return fail("could not create board page");
                }
                y = HPDF_Page_GetHeight(page) - k_top_margin;
            }

            if (new_category) {
                category_pages.emplace_back(board.category, page_number);
                if (opt.bookmarks && !add_bookmark(pdf, page, board.category)) {
                    return fail("could not create PDF bookmark");
                }
                if (!write_section_heading(page, bold_font, y, board.category)) {
                    return fail("could not render category heading");
                }
                previous_category = *real_category;
            }

            if (!write_board(board, page, y, regular_font, bold_font, mono_font, add_page)) {
                return fail("could not render board content");
            }
        }

        if (toc_page != nullptr && !write_toc(toc_page, regular_font, bold_font, category_pages)) {
            return fail("could not render table of contents");
        }

        if (HPDF_SaveToStream(pdf) != HPDF_OK) {
            return fail("could not create PDF byte stream");
        }

        HPDF_UINT32 size = HPDF_GetStreamSize(pdf);
        std::string bytes(size, '\0');
        HPDF_UINT32 read_size = size;
        if (HPDF_ReadFromStream(
                pdf,
                reinterpret_cast<HPDF_BYTE*>(bytes.data()),
                &read_size
            ) != HPDF_OK) {
            return fail("could not read PDF byte stream");
        }

        bytes.resize(read_size);
        HPDF_Free(pdf);
        return {true, std::move(bytes), {}};
    }

    static std::optional<std::string> cate_to_real(const std::string& category) {
        static const std::unordered_map<std::string, std::string> category_map = {
            {"基础算法", "basic"}, {"基础", "basic"},
            {"数据结构", "data_structure"}, {"图论", "graph"},
            {"字符串", "string"}, {"数学与数论", "math"},
            {"数学", "math"}, {"数论", "math"}, {"动态规划", "dp"},
            {"dp", "dp"}, {"贪心与构造", "greedy"}, {"贪心", "greedy"},
            {"构造", "greedy"}, {"计算几何", "geometry"},
            {"几何", "geometry"}, {"随机化与启发式", "randomized"},
            {"随机", "randomized"}, {"随机化", "randomized"},
            {"启发式", "randomized"}, {"综合与其他算法", "other"},
            {"综合", "other"}, {"其他", "other"}
        };

        const auto it = category_map.find(category);
        if (it == category_map.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    static constexpr HPDF_REAL k_left_margin = 48;
    static constexpr HPDF_REAL k_top_margin = 52;
    static constexpr HPDF_REAL k_bottom_margin = 48;

    static bool fill_rect(
        HPDF_Page page,
        HPDF_REAL x,
        HPDF_REAL y,
        HPDF_REAL width,
        HPDF_REAL height,
        HPDF_REAL red,
        HPDF_REAL green,
        HPDF_REAL blue
    ) {
        return HPDF_Page_SetRGBFill(page, red, green, blue) == HPDF_OK &&
            HPDF_Page_Rectangle(page, x, y, width, height) == HPDF_OK &&
            HPDF_Page_Fill(page) == HPDF_OK;
    }

    static bool draw_line(
        HPDF_Page page,
        HPDF_Font font,
        HPDF_REAL size,
        HPDF_REAL x,
        HPDF_REAL y,
        const std::string& text,
        HPDF_REAL red = 0.10,
        HPDF_REAL green = 0.13,
        HPDF_REAL blue = 0.19
    ) {
        return HPDF_Page_SetRGBFill(page, red, green, blue) == HPDF_OK &&
            HPDF_Page_BeginText(page) == HPDF_OK &&
            HPDF_Page_SetFontAndSize(page, font, size) == HPDF_OK &&
            HPDF_Page_TextOut(page, x, y, text.c_str()) == HPDF_OK &&
            HPDF_Page_EndText(page) == HPDF_OK;
    }

    static bool draw_centered(
        HPDF_Page page,
        HPDF_Font font,
        HPDF_REAL size,
        HPDF_REAL y,
        const std::string& text,
        HPDF_REAL red = 0.10,
        HPDF_REAL green = 0.13,
        HPDF_REAL blue = 0.19
    ) {
        if (HPDF_Page_BeginText(page) != HPDF_OK ||
            HPDF_Page_SetFontAndSize(page, font, size) != HPDF_OK) {
            return false;
        }
        const HPDF_REAL x = (HPDF_Page_GetWidth(page) - HPDF_Page_TextWidth(page, text.c_str())) / 2;
        if (HPDF_Page_EndText(page) != HPDF_OK) {
            return false;
        }
        return draw_line(page, font, size, x, y, text, red, green, blue);
    }

    static bool write_section_heading(
        HPDF_Page page,
        HPDF_Font font,
        HPDF_REAL& y,
        const std::string& title
    ) {
        if (!fill_rect(page, k_left_margin, y - 6, 6, 29, 0.12, 0.39, 0.71) ||
            !draw_line(page, font, 22, k_left_margin + 17, y, title, 0.08, 0.16, 0.30)) {
            return false;
        }
        y -= 40;
        return true;
    }

    // Expand tabs before measuring/wrapping: libHaru draws control characters
    // as font glyphs instead of treating them as layout instructions.
    static std::string expand_tabs(const std::string& line, HPDF_Font font) {
        const HPDF_UINT space_width = HPDF_Font_TextWidth(font,
            reinterpret_cast<const HPDF_BYTE*>(" "), 1).width;
        if (space_width == 0) {
            // Defensive fallback for fonts with no usable space metrics.
            std::string result;
            for (char ch : line) result += ch == '\t' ? "    " : std::string(1, ch);
            return result;
        }
        const HPDF_UINT tab_width = 4 * space_width;
        HPDF_UINT width = 0;
        std::string result;
        std::size_t begin = 0;
        while (begin < line.size()) {
            const auto tab = line.find('\t', begin);
            const auto end = tab == std::string::npos ? line.size() : tab;
            const auto segment = line.substr(begin, end - begin);
            result += segment;
            width += HPDF_Font_TextWidth(font,
                reinterpret_cast<const HPDF_BYTE*>(segment.data()),
                static_cast<HPDF_UINT>(segment.size())).width;
            if (tab == std::string::npos) break;
            const auto spaces = (tab_width - width % tab_width + space_width - 1) / space_width;
            result.append(spaces, ' ');
            width += spaces * space_width;
            begin = tab + 1;
        }
        return result;
    }

    template <typename AddPage>
    static bool write_wrapped(
        HPDF_Page current_page,
        HPDF_Font font,
        HPDF_REAL size,
        HPDF_REAL& y,
        const std::string& text,
        HPDF_REAL leading,
        HPDF_Page& page_ref,
        HPDF_REAL& y_ref,
        AddPage& add_page,
        bool code_background = false
    ) {
        constexpr HPDF_REAL k_right_margin = 48;
        std::size_t begin = 0;
        while (begin <= text.size()) {
            const std::size_t end = text.find('\n', begin);
            std::string remaining = text.substr(begin, end - begin);
            if (!remaining.empty() && remaining.back() == '\r') {
                remaining.pop_back(); // CRLF input: CR must not become a glyph.
            }
            remaining = expand_tabs(remaining, font);


            do {
                if (y < k_bottom_margin + leading) {
                    page_ref = add_page();
                    if (page_ref == nullptr) {
                        return false;
                    }
                    current_page = page_ref;
                    y = HPDF_Page_GetHeight(current_page) - k_top_margin;
                    y_ref = y;
                }

                std::size_t count = remaining.size();
                if (!remaining.empty()) {
                    if (HPDF_Page_BeginText(current_page) != HPDF_OK ||
                        HPDF_Page_SetFontAndSize(current_page, font, size) != HPDF_OK) {
                        return false;
                    }
                    HPDF_REAL measured_width = 0;
                    count = HPDF_Page_MeasureText(
                        current_page,
                        remaining.c_str(),
                        HPDF_Page_GetWidth(current_page) - k_left_margin - k_right_margin,
                        HPDF_FALSE,
                        &measured_width
                    );
                    if (HPDF_Page_EndText(current_page) != HPDF_OK || count == 0) {
                        return false;
                    }

                    // libHaru 返回的是 UTF-8 字节位置，不能截断在多字节字符中间。
                    while (count < remaining.size() &&
                           (static_cast<unsigned char>(remaining[count]) & 0xC0) == 0x80) {
                        --count;
                    }
                    if (count == 0) {
                        return false;
                    }
                }

                if (code_background &&
                    (!fill_rect(
                        current_page,
                        k_left_margin - 9,
                        y - 5,
                        HPDF_Page_GetWidth(current_page) - k_left_margin - k_right_margin + 18,
                        // 不能延伸到下一行的基线，否则会覆盖上一行 g、p、q、y 的下伸部分。
                        leading,
                        0.94, 0.96, 0.99
                    ) ||
                    !fill_rect(current_page, k_left_margin - 9, y - 5, 3, leading, 0.12, 0.39, 0.71))) {
                    return false;
                }
                if (!draw_line(current_page, font, size, k_left_margin, y, remaining.substr(0, count))) {
                    return false;
                }
                remaining.erase(0, count);
                y -= leading;
                y_ref = y;
            } while (!remaining.empty());

            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
        return true;
    }

    template <typename AddPage>
    static bool write_board(
        const Board& board,
        HPDF_Page& page,
        HPDF_REAL& y,
        HPDF_Font regular_font,
        HPDF_Font bold_font,
        HPDF_Font mono_font,
        AddPage& add_page
    ) {
        if (!write_wrapped(page, bold_font, 19, y, board.title, 27, page, y, add_page) ||
            !write_wrapped(page, regular_font, 12, y,
                "难度：" + board.difficulty + "    语言：" + board.language,
                18, page, y, add_page) ||
            !write_wrapped(page, bold_font, 13, y, "算法说明", 20, page, y, add_page) ||
            !write_wrapped(page, regular_font, 12, y, board.description, 19, page, y, add_page) ||
            !add_vertical_space(page, y, 26, add_page) ||
            !write_wrapped(page, bold_font, 13, y, "参考代码", 20, page, y, add_page) ||
            !write_wrapped(page, mono_font, 12.5, y, board.code, 18, page, y, add_page, true)) {
            return false;
        }
        y -= 16;
        return true;
    }

    template <typename AddPage>
    static bool add_vertical_space(HPDF_Page& page, HPDF_REAL& y, HPDF_REAL space, AddPage& add_page) {
        if (y - space >= k_bottom_margin) {
            y -= space;
            return true;
        }
        page = add_page();
        if (page == nullptr) {
            return false;
        }
        y = HPDF_Page_GetHeight(page) - k_top_margin;
        return true;
    }

    static bool add_bookmark(HPDF_Doc pdf, HPDF_Page page, const std::string& title) {
        HPDF_Encoder utf8 = HPDF_GetEncoder(pdf, "UTF-8");
        if (utf8 == nullptr) {
            return false;
        }
        HPDF_Outline outline = HPDF_CreateOutline(pdf, nullptr, title.c_str(), utf8);
        if (outline == nullptr) {
            return false;
        }
        HPDF_Destination destination = HPDF_Page_CreateDestination(page);
        return destination != nullptr &&
            HPDF_Destination_SetFit(destination) == HPDF_OK &&
            HPDF_Outline_SetDestination(outline, destination) == HPDF_OK;
    }

    static bool write_toc(
        HPDF_Page page,
        HPDF_Font regular_font,
        HPDF_Font bold_font,
        const std::vector<std::pair<std::string, int>>& category_pages
    ) {
        HPDF_REAL y = HPDF_Page_GetHeight(page) - k_top_margin;
        if (!draw_centered(page, bold_font, 24, y, "目录", 0.08, 0.16, 0.30)) {
            return false;
        }
        y -= 42;

        const HPDF_REAL right_edge = HPDF_Page_GetWidth(page) - k_left_margin;
        for (const auto& [category, page_number] : category_pages) {
            const std::string page_text = std::to_string(page_number);
            const auto category_width = measure_text_width(page, regular_font, 14, category);
            const auto page_width = measure_text_width(page, regular_font, 14, page_text);
            const auto dot_width = measure_text_width(page, regular_font, 14, "·");
            if (!category_width || !page_width || !dot_width) {
                return false;
            }

            const HPDF_REAL page_x = right_edge - *page_width;
            const HPDF_REAL dots_x = k_left_margin + *category_width + 14;
            const HPDF_REAL dots_right = page_x - 14;
            const int dot_count = std::max(
                0,
                static_cast<int>((dots_right - dots_x) / *dot_width)
            );
            std::string dots;
            for (int i = 0; i < dot_count; ++i) {
                dots += "·";
            }

            if (!draw_line(page, regular_font, 14, k_left_margin, y, category, 0.22, 0.28, 0.38) ||
                (!dots.empty() && !draw_line(page, regular_font, 14, dots_x, y, dots, 0.42, 0.47, 0.55)) ||
                !draw_line(page, regular_font, 14, page_x, y, page_text, 0.22, 0.28, 0.38)) {
                return false;
            }
            y -= 28;
        }
        return true;
    }

    static std::optional<HPDF_REAL> measure_text_width(
        HPDF_Page page,
        HPDF_Font font,
        HPDF_REAL size,
        const std::string& text
    ) {
        if (HPDF_Page_BeginText(page) != HPDF_OK ||
            HPDF_Page_SetFontAndSize(page, font, size) != HPDF_OK) {
            return std::nullopt;
        }
        const HPDF_REAL width = HPDF_Page_TextWidth(page, text.c_str());
        if (HPDF_Page_EndText(page) != HPDF_OK) {
            return std::nullopt;
        }
        return width;
    }

    static void boards_sort(const json& data, std::vector<Board>& boards) {
        std::unordered_map<std::string, int> category_rank;
        for (std::size_t i = 0; i < data.size(); ++i) {
            category_rank[data[i].get<std::string>()] = static_cast<int>(i);
        }

        std::sort(
            boards.begin(),
            boards.end(),
            [&](const Board& left, const Board& right) {
                const auto left_category = cate_to_real(left.category);
                const auto right_category = cate_to_real(right.category);
                const int left_rank = category_rank.at(*left_category);
                const int right_rank = category_rank.at(*right_category);
                return left_rank != right_rank ? left_rank < right_rank : left.title < right.title;
            }
        );
    }
};
