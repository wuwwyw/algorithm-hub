#pragma once
#include <iostream>
#include <string>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <sys/socket.h>

const int MAX_REQUEST_SIZE = 1024 * 1024;

enum class IO_resp {
    FINISHED,
    PARTIAL,
    ERROR,
    CLOSED
};

enum class IO_reqt {
    OK,
    AGAIN,
    ERROR,
    CLOSED
};

class IO {
public:
    IO() = default;
    IO(int fd) : _fd(fd) {}

    IO_resp send_mess(std::string& response) {
        size_t sent = 0;
        while (sent < response.size()) {
            ssize_t n = send(_fd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                return IO_resp::ERROR;
            } else if (n == 0) {
                return IO_resp::CLOSED;
            }
            sent += n;
        }
        response.erase(0, sent);
        if (!response.empty()) return IO_resp::PARTIAL;
        return IO_resp::FINISHED;
    }

    IO_reqt recv_mess(std::string &request) {
        bool flag = false;
        while (true) {
            if (request.size() > MAX_REQUEST_SIZE) {
                return IO_reqt::ERROR;
            }

            char buffur[4096];
            const std::size_t remaining = MAX_REQUEST_SIZE - request.size();
            ssize_t n = recv(_fd, buffur, std::min(sizeof(buffur), remaining), 0);

            if (n < 0) {
                if (errno == EINTR) continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK) return flag ? IO_reqt::OK : IO_reqt::AGAIN;
                return IO_reqt::ERROR;
            } else if (n == 0) {
                return IO_reqt::CLOSED;
            }
            flag = true;
            request.append(buffur, n);
        }
    }

private:
    int _fd;
};
