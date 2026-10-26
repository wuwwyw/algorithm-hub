#pragma once
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>


class InetAddr {
public:
    InetAddr () {
        _len = sizeof(_addr);
    }

    InetAddr(uint16_t port, const std::string& ip) : _port(port), _ip(ip) {
        _addr.sin_family = AF_INET;
        _addr.sin_port = htons(_port);
        inet_pton(AF_INET, _ip.c_str(), &_addr.sin_addr);
        _len = sizeof(_addr); 
    }

    ~InetAddr() {}

    void create_addr(uint16_t port, const std::string& ip) {
        _port = port;
        _ip = ip;
        _addr.sin_family = AF_INET;
        _addr.sin_port = htons(_port);
        inet_pton(AF_INET, _ip.c_str(), &_addr.sin_addr);
        _len = sizeof(_addr);
    }

    struct sockaddr* get_addr() {
        return (struct sockaddr *) &_addr;
    }

    socklen_t* get_len() {
        return &_len;
    }

private:
    uint16_t _port;
    std::string _ip;
    struct sockaddr_in _addr;
    socklen_t _len;
};
