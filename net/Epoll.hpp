#pragma once
#include <vector>
#include <sys/epoll.h>

class Epoller {
public:
    Epoller() {
        _epfd = epoll_create1(EPOLL_CLOEXEC);
        if (_epfd == -1) {
            throw std::runtime_error("epoll_create1 failed");
        }
    }

    ~Epoller() {
        close(_epfd);
    }

    bool add(int fd, uint32_t events) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;

        return epoll_ctl(_epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
    }

    bool mod(int fd, uint32_t events) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;

        return epoll_ctl(_epfd, EPOLL_CTL_MOD, fd, &ev) == 0;
    }

    bool del(int fd) {
        return epoll_ctl(_epfd, EPOLL_CTL_DEL, fd, nullptr) == 0;
    }

    int wait(std::vector<epoll_event> &events) {
        return epoll_wait(_epfd, events.data(), events.size(), -1);
    }
private:
    int _epfd;
};