#pragma once
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include "Epoll.hpp"
#include "Http_prase.hpp"
#include "Trie_route.hpp"
#include "Client_conn.hpp"
#include "ThreadPool.hpp"

using Clientit = std::unordered_map<int, std::shared_ptr<Client_conn>>::iterator;
using IdleEntry = std::pair<ST_Clock::time_point, int>;

const size_t mysql_pool_sz = 12;
const size_t redis_pool_sz = 8;
const uint32_t event_sz = 1024;

class Event_loop {
public:
    Event_loop(
        MysqlPool &mysql_pool, 
        RedisPool &redis_pool, 
        Trie_route &tr, 
        ThreadPool &thread_pool, 
        int idx, 
        std::vector<int> &bell,
        std::mutex &mutex
    ) : _mysql_pool(mysql_pool), _redis_pool(redis_pool), _tr(tr), _thread_pool(thread_pool), _loop_idx(idx), _bell(bell), _mutex(mutex) {
            _notify_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (_notify_fd < 0 || _loop_idx < 0) throw std::runtime_error("eventfd failed");
            _epoll.add(_loop_idx, EPOLLIN);
            _epoll.add(_notify_fd, EPOLLIN);

            _timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
            if (_timer_fd < 0) throw std::runtime_error("timerfd failed");

            struct itimerspec spec{};
            spec.it_interval.tv_sec = 1;
            spec.it_value.tv_sec = 1;
            if (timerfd_settime(_timer_fd, 0, &spec, nullptr) < 0) {
                throw std::runtime_error("timerfd_settime failed");
            }
            _epoll.add(_timer_fd, EPOLLIN);
        }
    
    ~Event_loop() {
        if (_notify_fd >= 0) close(_notify_fd);
        if (_timer_fd >= 0) close(_timer_fd);
    }

    void run() {
        while (!_stop) {
            std::vector<epoll_event> events(event_sz);
            int n = _epoll.wait(events);
            for (int i = 0; i < n; i++) {
                int fd = events[i].data.fd;

                if (fd == _notify_fd) {
                    clear_fd();
                }else if (fd == _timer_fd) {
                    handle_time();
                } else if (fd == _loop_idx) {
                    uint64_t val;
                    read(fd, &val, sizeof(val));
                    add_client();
                } else {
                    auto it = _clients.find(fd);
                    if (it == _clients.end()) continue;

                    auto client = it->second;
                    uint32_t revents = events[i].events;

                    if (client->worker_busy()) {
                        if (revents & (EPOLLERR | EPOLLHUP)) {
                            client->request_close();
                        }

                        continue;
                    }

                    if (revents & EPOLLERR) {
                        int err = 0;
                        socklen_t len = sizeof(err);

                        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
                            std::cerr << "socket error, fd=" << fd << ", error=" << err << ", message=" << strerror(err) << '\n';
                        }

                        _epoll.del(fd);
                        client->closed();
                        _clients.erase(it);
                        continue;
                    }
                    if ((revents & EPOLLHUP) && !(revents & EPOLLIN)) {
                        _epoll.del(fd);
                        client->closed();
                        _clients.erase(it);
                        continue;
                    }

                    if (revents & EPOLLIN) {
                        auto flag = client->client_recv();
                    
                        if (flag == IO_reqt::OK) {
                            try_submit(it);
                        } else if (flag == IO_reqt::ERROR) {
                            int fd = it->first;
                            std::cerr << "recv error, fd=" << fd << std::endl;

                            _epoll.del(fd);
                            it->second->closed();
                            _clients.erase(it);
                        } else if (flag == IO_reqt::CLOSED) {
                            int fd = it->first;

                            _epoll.del(fd);
                            it->second->closed();
                            _clients.erase(it);
                        }
                    } else if (revents & EPOLLOUT) {
                        auto flag = client->client_send();

                        if (flag == IO_resp::ERROR || flag == IO_resp::CLOSED) {
                            _epoll.del(fd);
                            client->closed();
                            _clients.erase(it);
                        } else if (flag == IO_resp::FINISHED) {
                            client->reset_client();

                            if (!try_submit(it)) {
                                if (!_epoll.mod(fd, EPOLLIN)) {
                                    std::cerr << "epoll mod EPOLLIN failed, fd=" << fd << ", error=" << strerror(errno) << '\n';

                                    _epoll.del(fd);
                                    client->closed();
                                    _clients.erase(it);
                                    continue;
                                }
                            }
                        }
                    }
                }
            } 
        }
    }

public:
    static bool SetNonBlock(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            perror("fcntl get");
            return false;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            perror("fcntl set");
            return false;
        }
        return true;
    }

    void stop() {
        _stop = true;
        notify();
    }

private:
    void notify() {
        uint64_t one = 1;
        write(_notify_fd, &one, sizeof(one));
    }

    void add_client() {
        std::vector<int> pending;
        {
            std::lock_guard <std::mutex> lock(_mutex);
            pending.swap(_bell);
        }
        for (const auto &client_fd : pending) {
            _epoll.add(client_fd, EPOLLIN);
            _clients.try_emplace(client_fd, std::make_shared<Client_conn>(client_fd));

            _timer_heap.push({
                ST_Clock::now() + Clock::seconds(IDLE_TIMEOUT_SEC),
                client_fd
            });
        }
    }

    bool try_submit(Clientit it) {
        auto client = it->second;
        int fd = it->first;

        if ((client->check_status() == ConnState::READING_HEADER && client->check_post()) 
                || client->check_status() == ConnState::READING_BODY) {

            client->set_worker_busy(true);
            if (!_epoll.mod(fd, 0)) {
                std::cerr << "epoll mod failed, fd=" << fd << ", error=" << strerror(errno) << std::endl;
                _epoll.del(fd);
                client->closed();
                _clients.erase(it);
                return true;
            }
            if (client->check_status() == ConnState::READING_HEADER)
                client->set_state(ConnState::PARSING_HEADER);

            _thread_pool.submit([client, this, fd] () {
                try {
                    client->client_oper(_epoll, fd, _tr);
                } catch (const std::exception& e) {
                    std::cerr << "client_oper exception, fd=" << fd << ": " << e.what() << '\n';
                    client->request_close();
                } catch (...) {
                    std::cerr << "client_oper unknown exception, fd=" << fd << '\n';
                    client->request_close();
                }
                
                client->set_worker_busy(false);
                if (client->close_requested()) {
                    this->notify();
                }
            });
            return true;
        }
        return false;
    }

    void handle_time() {
        uint64_t expirations = 0;
        read(_timer_fd, &expirations, sizeof(expirations));

        auto now = ST_Clock::now();
        const auto timeout = Clock::seconds(IDLE_TIMEOUT_SEC);
        while (!_timer_heap.empty()) {
            auto [expire, fd] = _timer_heap.top();
            if (expire > now) break;

            _timer_heap.pop();
            auto it = _clients.find(fd);
            if (it == _clients.end()) continue;

            auto client = it->second;
            if (client->worker_busy()) {
                _timer_heap.push({now + timeout, fd});
                continue;
            }

            if (client->last_active() + timeout > now) {
                _timer_heap.push({client->last_active() + timeout, fd});
                continue;
            }

            _epoll.del(fd);
            client->closed();
            _clients.erase(it);
        }
    }

    void clear_fd() {
        uint64_t val;
        read(_notify_fd, &val, sizeof(val));
        for (auto it = _clients.begin(); it != _clients.end();) {
            auto& client = it->second;
            if (!client->worker_busy() && client->close_requested()) {
                _epoll.del(it->first);
                client->closed();
                it = _clients.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    int _notify_fd = -1;
    int _timer_fd = -1;
    int _loop_idx = -1;
    Epoller _epoll;
    std::priority_queue<IdleEntry, std::vector<IdleEntry>, std::greater<>> _timer_heap;
    std::unordered_map <int,std::shared_ptr<Client_conn>> _clients;
    std::atomic <bool> _stop = false;
    std::mutex &_mutex;
    std::vector<int> &_bell;
    MysqlPool &_mysql_pool;
    RedisPool &_redis_pool;
    Trie_route &_tr;
    ThreadPool &_thread_pool;
};