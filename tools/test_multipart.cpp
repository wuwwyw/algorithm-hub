#include <vector>
#include <cassert>
#include "../net/Http_prase.hpp"

int main() {
    // Browser FormData: text fields have one header; file fields have two.
    const std::string boundary = "----WebKitFormBoundaryRegression";
    std::string code = "#include <bits/stdc++.h>\r\n";
    for (int i = 0; i < 400; ++i)
        code += "\tauto query = [&] (auto &&self) -> int { return 0; };\r\n";
    code += "// 中文注释\r\n";
    code += "\r\n--" + boundary + "not-a-boundary\r\n--" + boundary + "--still-code\r\n";
    for (const auto& mime : {"text/plain", "application/octet-stream", "text/x-c++src"}) {
        for (bool quoted : {false, true}) {
            Http_parse parser;
            std::string request = "POST /api/my/boards HTTP/1.1\r\nContent-Type: multipart/form-data; boundary="
                + (quoted ? "\"" + boundary + "\"" : boundary) + "\r\n\r\n";
            assert(parser.parse(request));
            const std::string body = "--" + boundary
                + "\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\naaa\r\n--" + boundary
                + "\r\nContent-Disposition: form-data; name=\"code\"; filename=\"C.cpp\"\r\nContent-Type: "
                + mime + "\r\n\r\n" + code + "\r\n--" + boundary + "--\r\n";
            std::vector<multipart> parts;
            assert(parser.parse_multipart(body, parts));
            assert(parts.size() == 2);
            assert(parts[0].name == "title" && parts[0].content == "aaa");
            assert(parts[1].name == "code" && parts[1].filename == "C.cpp");
            assert(parts[1].content_type == mime && parts[1].content == code);
            assert(!parser.parse_multipart(body.substr(0, body.size() - boundary.size() - 8), parts));
        }
    }
    std::cout << "PASS: browser file uploads, MIME types, quoted boundary, >4KB code, exact bytes, truncated input\n";
}
