// Run from the project root:
// g++ -std=c++17 tools/test_pdf_export.cpp -lhpdf -o build/test_pdf_export
// build/test_pdf_export build/test_pdf_tabs.pdf
#define ALGORITHM_HUB_PDF_REGULAR_FONT_PATH "assets/fonts/LXGWWenKai-Regular.ttf"
#define ALGORITHM_HUB_PDF_BOLD_FONT_PATH "assets/fonts/LXGWWenKai-Medium.ttf"
#define ALGORITHM_HUB_PDF_MONO_FONT_PATH "assets/fonts/LXGWWenKaiMono-Regular.ttf"
#include "../sql/Pdf_exporter.hpp"
#include <cassert>
#include <iostream>

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string code = "int main() {\r\n\tint a = 1;\t// comment\r\n"
        "\t\treturn a;\r\n \t// mixed indentation\r\n"
        "\t// 中文\tend\r\n\t\r\n}\r\n";
    std::vector<Board> boards = {{1, "Tab regression", "数学", "简单", "PDF whitespace check", code, "C++"}};
    Pdf_exporter exporter;
    auto result = exporter.generate(boards, json::array({"math"}), {false, false, false, false});
    assert(result.success && result.bytes.rfind("%PDF-", 0) == 0);
    assert(boards[0].code == code); // Export must preserve the stored code.
    std::ofstream output(argv[1], std::ios::binary);
    output.write(result.bytes.data(), result.bytes.size());
    assert(output.good());
    std::cout << "PDF whitespace fixture generated\n";
}
