#pragma once
#include <iostream>
#include <unordered_map>

class Http_response {
public:
    Http_response() = default;

    Http_response(int status_code, const std::string& status_code_text, const std::string& body, const std::string &path) 
                    : _status_code(status_code), _status_code_text(status_code_text), _body(body)
    {
        _headers["Content-Length"] = std::to_string(_body.size());
        _headers["Content-Type"] = content_type(path);
    }

    Http_response(int status_code, const std::string& status_code_text, const std::string& body) 
                    : _status_code(status_code), _status_code_text(status_code_text), _body(body)
    {
        _headers["Content-Length"] = std::to_string(_body.size());
    }

    std::string serialize() {
        std::string response;
        response = _version + " " + std::to_string(_status_code) + " " + _status_code_text + "\r\n";
        for (auto [k,v] : _headers) {
            response += k + ": " + v + "\r\n";
        }
        response += "\r\n";
        response += _body;
        return response;
    }

    void add_header(const std::string& key, const std::string& val) {
        _headers[key] = val;
    }

    std::string content_type(const std::string &path) {
        if ((path.size() >= 5 && path.substr(path.size() - 5) == ".html") || path == "/") {
            return "text/html";
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") {
            return "text/css";
        }
        if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") {
            return "application/javascript";
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") {
            return "image/png";
        }
        if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") {
            return "application/json";
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".jpg") {
            return "image/jpeg";
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") {
            return "image/svg+xml";
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".txt") {
            return "text/plain";
        }
        return "application/octet-stream";
    }

public:
    std::string _version = "HTTP/1.1";
    int _status_code = 200;
    std::string _status_code_text = "OK";
    std::unordered_map <std::string,std::string> _headers;
    std::string _body;
};