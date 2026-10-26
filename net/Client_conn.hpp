#pragma once
#include <iostream>
#include <atomic>
#include <chrono>
#include "IO.hpp"
#include "Http_prase.hpp"
#include "Http_response.hpp"

#define Clock std::chrono
using ST_Clock = Clock::steady_clock;


static constexpr int IDLE_TIMEOUT_SEC = 60;

enum class ConnState {
    READING_HEADER,  // 正在接收报头
    PARSING_HEADER,  // 报头已完整，工作线程正在解析
    READING_BODY,    // 已知道正文长度，正在接收正文
    PROCESSING,      // 完整请求正在被工作线程处理
    WRITING          // 正在发送响应
};

class Client_conn {
public:
    Client_conn(int client_fd) : _client_fd(client_fd) {
        _io = IO{_client_fd};
        _state = ConnState::READING_HEADER;
    }

    void client_oper(Epoller &epoll, int fd, Trie_route &tr) {
        if (check_status() == ConnState::PARSING_HEADER) {
            if (!client_parse()) {
                _htr = Http_response(400, "Bad Request", "Bad Request");
                _out_buffur = _htr.serialize();
                set_state(ConnState::WRITING);
                if (!epoll.mod(fd, EPOLLOUT)) {
                    request_close();
                }
                return ;
            }

            set_state(ConnState::READING_BODY);
            size_t post_len = post_size();
            _in_buffur.erase(0, post_len);

            size_t body_len = 0;
            auto it = _htp._headers.find("Content-Length");
            if (it == _htp._headers.end()) {
                set_size(body_len);
            } else {
                try {
                    const auto& length = it->second;
                    if (length.empty() || length.find_first_not_of("0123456789") != std::string::npos) {
                        throw std::invalid_argument("invalid Content-Length");
                    }
                    body_len = stoull(length);
                } catch (...) {
                    _htr = Http_response(400, "Bad Request", "Bad Request");
                    _out_buffur = _htr.serialize();
                    set_state(ConnState::WRITING);
                    if (!epoll.mod(fd, EPOLLOUT)) {
                        request_close();
                    }
                    return ;
                }
                set_size(body_len);
            }
        }

        if (!check_size()) {
            if (close_requested()) {
                return ;
            }

            if (!epoll.mod(fd, EPOLLIN)) {
                std::cerr << "epoll mod EPOLLIN failed, fd=" << fd << ", error=" << strerror(errno) << '\n';

                request_close();
            }
            return ;
        }
        
        _htp._body = _in_buffur.substr(0, _required_size);
        // Consume even rejected bodies so keep-alive can parse the next request.
        _in_buffur.erase(0, _required_size);
        if (!get_body()) {
            _htr = Http_response(400, "Bad Request",
                json({{"code", 400}, {"message", _body_error}}).dump());
            _htr.add_header("Content-Type", "application/json");
            _out_buffur = _htr.serialize();
            set_state(ConnState::WRITING);

            if (!epoll.mod(fd, EPOLLOUT)) {
                request_close();
            }

            return;
        }
        
        set_state(ConnState::PROCESSING);
        client_response(tr);
        set_state(ConnState::WRITING);

        if (!epoll.mod(fd, EPOLLOUT)) {
            std::cerr << "epoll mod EPOLLOUT failed, fd=" << fd << ", error=" << strerror(errno) << '\n';

            request_close();
            return ;
        }
    }

    void reset_client() {
        set_state(ConnState::READING_HEADER);
        _htp = Http_parse{};
        _body_error = "上传数据格式不正确，请重新选择代码文件后重试。";
    }

    IO_resp client_send() {
        IO_resp res = _io.send_mess(_out_buffur);
        if (res == IO_resp::FINISHED || res == IO_resp::PARTIAL) {
            touch();
        }
        return res;
    }

    IO_reqt client_recv() {
        IO_reqt req = _io.recv_mess(_in_buffur);
        if (req == IO_reqt::OK) {
            touch();
        }
        return req;
    }

    bool client_parse() {
        return _htp.parse(_in_buffur);
    }

    void client_response(Trie_route &tr) {

        _htr = tr.handle(_htp._method, _htp._path, _htp._body, _htp._headers);
        _out_buffur = _htr.serialize();
    }

    ConnState check_status() {
        return _state;
    }

    void set_state(ConnState a) {
        _state = a;
    }

    void set_size(size_t sz) {
        _required_size = sz;
    }

    bool check_post() {
        if (_in_buffur.find(post_gesp) != std::string::npos) return true;
        return false;
    }

    bool check_size() {
        return _in_buffur.size() >= _required_size;
    }

    size_t post_size() {
        return _in_buffur.find(post_gesp) + post_gesp.size();
    }

    void touch() {
        _last_active = ST_Clock::now();
    }

    ST_Clock::time_point last_active() {
        return _last_active;
    }

    void closed() {
        close(_client_fd);
    }

    bool worker_busy() const {
        return _worker_busy.load();
    }

    void set_worker_busy(bool busy) {
        _worker_busy.store(busy);
    }

    bool close_requested() const {
        return _close_requested.load();
    }

    void request_close() {
        _close_requested.store(true);
    }

private:
    bool get_body() {
        auto it = _htp._headers.find("Content-Type");
        if (it == _htp._headers.end()) return true;

        if (it->second != "multipart/form-data") {
            return true;
        }

        std::vector <multipart> parts;
        if (!_htp.parse_multipart(_htp._body, parts)) {
            return false;
        }

        json data = {
            {"title", ""},
            {"category", ""},
            {"difficulty", ""},
            {"language", ""},
            {"description", ""},
            {"code", ""}
        };

        for (const auto &part : parts) {
            if (!part.filename.empty()) {
                data["code"] = part.content;
            } else if (
                part.name == "title" ||
                part.name == "category" ||
                part.name == "difficulty" ||
                part.name == "language" ||
                part.name == "description" ||
                part.name == "code"
            ) {
                data[part.name] = part.content;
            }
        }
        try {
            _htp._body = data.dump();
        } catch (const json::type_error&) {
            _body_error = "代码文件不是有效的 UTF-8 文本，请转为 UTF-8 编码后上传。";
            return false;
        }
        _htp._headers["Content-Type"] = "application/json";
        return true;
    }

private:
    std::string _body_error = "上传数据格式不正确，请重新选择代码文件后重试。";
    int _client_fd;
    std::string _in_buffur;
    std::string _out_buffur;
    IO _io;
    Http_parse _htp;
    Http_response _htr;
    ConnState _state;
    size_t _required_size = 0;
    std::atomic<bool> _worker_busy{false};
    std::atomic<bool> _close_requested{false};
    ST_Clock::time_point _last_active{ST_Clock::now()};
};
