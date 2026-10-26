#pragma once
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "Inetaddr.hpp"


enum {
    SUCCESS,
    SOCKET_FAIL,
    BIND_FAIL,
    LISTEN_FAIL
};

class Socket {
public:
    static constexpr int DEFAULT_LISTEN_BACKLOG = 1024;

    Socket() {}
    ~Socket() {
        close(_sockfd);
    }

    void Create_socket() {
        _sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (_sockfd == -1) {
            std::cerr << "create sockfd error" << std::endl;
            exit(SOCKET_FAIL);
        }
    }

    void BindAndListen(uint16_t port, const std::string& ip,
                       int backlog = DEFAULT_LISTEN_BACKLOG) {
        InetAddr server_addr(port, ip);
        if (bind(_sockfd, server_addr.get_addr(), sizeof(*server_addr.get_addr())) == -1) {
            std::cerr << "bind error" << std::endl;
            exit(BIND_FAIL);
        }

        if (listen(_sockfd, backlog) == -1) {
            std::cerr << "listen error" << std::endl;
            exit(LISTEN_FAIL);
        }
    }

    int Get_fd() {
        return _sockfd;
    }

private:
    int _sockfd;
};
