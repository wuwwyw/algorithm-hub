#pragma once
#include <iostream>
#include <unordered_map>
#include <vector>

const std::string row_gesp = "\r\n";
const std::string post_gesp = "\r\n\r\n";

struct multipart {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string content;
};

class Http_parse {
public:
    Http_parse() = default;

    bool parse(std::string& request) {
        //处理第一行
        size_t suf = request.find(row_gesp);
        if (suf == std::string::npos) return false;
        std::string first_row = request.substr(0, suf);
        if (!first_row_prase(first_row)) return false;
        //处理接下来的报头
        size_t pre = 0;
        pre = suf + row_gesp.size();
        while (true) {
            suf = request.find(row_gesp, pre);
            if (suf == std::string::npos) {
                return false;
            }
            if (suf == pre) {
                break;
            }
            std::string row = request.substr(pre, suf - pre);
            if (!oper_post(row)) {
                return false;
            }
            pre = suf + row_gesp.size();
        }
        return true;
    }

    bool parse_multipart(
        const std::string &body,
        std::vector<multipart> &parts
    ) {
        parts.clear();

        if (_boundary.size() >= 2 && _boundary.front() == '"' && _boundary.back() == '"') {
            _boundary = _boundary.substr(1, _boundary.size() - 2);
        }

        if (_boundary.empty()) {
            return false;
        }

        const std::string delimiter = "--" + _boundary;
        size_t pos = 0;
        while (true) {
            pos = body.find(delimiter, pos);
            if (pos == std::string::npos || pos + delimiter.size() + 2 > body.size()) {
                return false;
            }

            pos += delimiter.size();

            if (body.compare(pos, 2, "--") == 0) break;
            if (body.compare(pos, 2, "\r\n") != 0) return false;
            
            pos += 2;

            size_t header_end = body.find("\r\n\r\n", pos);
            if (header_end == std::string::npos) return false;

            std::string part_header = body.substr(pos, header_end - pos + 2);
            pos = header_end + 4;
            size_t val_end = body.find("\r\n" + delimiter, pos);
            // A boundary prefix inside file content is not a delimiter.
            while (val_end != std::string::npos) {
                const auto suffix = val_end + 2 + delimiter.size();
                if (body.compare(suffix, 2, "\r\n") == 0 ||
                    (body.compare(suffix, 2, "--") == 0 &&
                     (suffix + 2 == body.size() || body.compare(suffix + 2, 2, "\r\n") == 0))) break;
                val_end = body.find("\r\n" + delimiter, suffix);
            }
            if (val_end == std::string::npos) return false;

            std::string part_val = body.substr(pos, val_end - pos);
            pos = val_end + 2;

            if (!create_part(part_header, part_val, parts)) {
                return false;
            }
        }
        return true;
    }


private:
    bool first_row_prase(const std::string& first_row) {
        size_t pre = 0;
        size_t suf = first_row.find(" ", pre);
        if (suf == std::string::npos) return false;
        _method = first_row.substr(pre,suf - pre);
        pre = suf + 1;
        suf = first_row.find(" ", pre);
        if (suf == std::string::npos) return false;
        _path = first_row.substr(pre,suf - pre);
        pre = suf + 1;
        _version = first_row.substr(pre);
        return true;
    }

    bool oper_post(const std::string& row) {
        size_t pos = row.find(": ");
        if (pos == std::string::npos) {
            return false;
        }
        std::string k = row.substr(0, pos);
        pos += 2;
        std::string v = row.substr(pos);
        size_t mid;
        if ((mid = v.find("; ")) != std::string::npos && k == "Content-Type") {
            std::string tem_v = v.substr(0, mid);
            _headers[k] = tem_v;
            tem_v = v.substr(mid + 2);
            mid = tem_v.find("=");
            _boundary = tem_v.substr(mid + 1);
        } else {
            _headers[k] = v;
        }
        return true;
    }

    bool create_part(const std::string &header, const std::string &val, std::vector<multipart> &parts) {
        multipart part;

        size_t suf = 0, pre = 0;
        do
        {
            suf = header.find("\r\n", pre);
            std::string one_line = header.substr(pre, suf - pre) + "; ";

            size_t now = one_line.find(": ");
            if (now == std::string::npos) {
                return false;
            }

            std::string tem_k = one_line.substr(0, now);
            size_t pos = one_line.find("; ");
            std::string tem_v;
            tem_v = one_line.substr(now + 2, pos - now - 2);
            one_line.erase(0, pos + 2);
            judge_part(tem_k, tem_v, part);

            inner_oper(one_line, part);

            // Advance past the complete header line, not just two bytes.
            // File parts include both Content-Disposition and Content-Type.
            pre = suf + 2;
        } while (pre < header.size());
        
        part.content = val;
        parts.push_back(std::move(part));
        return true;
    }

    void inner_oper(const std::string &str, multipart &part) {
        size_t tem_suf = 0,tem_pre = 0, tem_mid = 0;
                    
        while ((tem_mid = str.find("=", tem_pre)) != std::string::npos) {
            tem_suf = str.find("; ", tem_mid);
            std::string tem = str.substr(tem_mid + 1, tem_suf - tem_mid - 1);
            if (tem.size() >= 2 && tem.front() == '\"' && tem.back() == '\"') {
                tem = tem.substr(1,tem.size() - 2);
            }
            judge_part(str.substr(tem_pre, tem_mid - tem_pre), tem, part);
            if (tem_suf == std::string::npos) {
                break;
            }
            tem_pre = tem_suf + 2;
        }
    }

    void judge_part(const std::string &k, const std::string &v, multipart &part) {
        if (k == "name") {
            part.name = v;
        } else if (k == "filename") {
            part.filename = v;
        } else if (k == "Content-Type") {
            part.content_type = v;
        }
    }

public:
    std::string _method;
    std::string _path;
    std::string _version;
    std::unordered_map <std::string, std::string> _headers;
    std::string _body;
    std::string _boundary = "";
};