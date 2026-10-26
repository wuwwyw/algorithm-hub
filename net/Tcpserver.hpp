#pragma once
#include <array>
#include <cerrno>
#include <cstring>
#include "Server_config.hpp"
#include "Socket.hpp"
#include "Event_loop.hpp"

const int loop_size = 3;


class TcpServer {
public:
    TcpServer(uint16_t port, const std::string& ip,
              const ServerConfig& config = ServerConfig::load()) : _config(config) {
        _socket.Create_socket();
        _socket.BindAndListen(port, ip);
        if (!Event_loop::SetNonBlock(_socket.Get_fd())) {
            throw std::runtime_error("Failed to set listening socket non-blocking");
        }
        _accept_epoll.add(_socket.Get_fd(), EPOLLIN);

        _loop_eventfd.resize(loop_size);
        _bell.resize(loop_size);

        for (int i = 0; i < loop_size; i++) {
            _loop_eventfd[i] = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            auto loop = std::make_unique<Event_loop>(_mysql_pool, _redis_pool, _tr, _thread_pool, _loop_eventfd[i], _bell[i], _bell_mutex[i]);
            auto* ptr = loop.get();
            _event_loop.push_back(std::move(loop));
            _event_thread.emplace_back([ptr] () {
                ptr->run();
            });
        }
    }

    ~TcpServer() {
        for (int i = 0; i < loop_size; i++) {
            _event_loop[i]->stop();
            _event_thread[i].join();
            close(_loop_eventfd[i]);
        }
    }

    void loop() {
        int idx = 0;
        while (true) {
            std::vector<epoll_event> events(event_sz);
            int n = _accept_epoll.wait(events);
            for (int i = 0; i < n; i++) {
                if (events[i].data.fd != _socket.Get_fd()) continue;

                while (true) {
                    InetAddr client_addr;
                    int client_fd = accept(_socket.Get_fd(), client_addr.get_addr(), client_addr.get_len());
                    if (client_fd < 0) {
                        if (errno == EINTR || errno == ECONNABORTED) continue;
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            std::cerr << "accept failed: " << strerror(errno) << '\n';
                        }
                        break;
                    }

                    if (!Event_loop::SetNonBlock(client_fd)) {
                        close(client_fd);
                        continue;
                    }

                    uint64_t val = 1;
                    {
                        std::lock_guard <std::mutex> lock(_bell_mutex[idx]);
                        _bell[idx].push_back(client_fd);
                    }
                    ssize_t notified;
                    do {
                        notified = write(_loop_eventfd[idx], &val, sizeof(val));
                    } while (notified < 0 && errno == EINTR);
                    if (notified < 0 && errno != EAGAIN) {
                        std::cerr << "connection notify failed: " << strerror(errno) << '\n';
                    }
                    idx = (idx + 1)%loop_size;
                }
            }
        }
    }
    
private:
    ServerConfig _config;
    Socket _socket;
    Epoller _accept_epoll;
    MysqlPool _mysql_pool{_config.db_host, _config.db_user, _config.db_password,
                          _config.db_name, _config.db_port, mysql_pool_sz};
    RedisPool _redis_pool{_config.redis_host, _config.redis_port, redis_pool_sz,
                          _config.redis_user, _config.redis_password};
    Trie_route _tr{_mysql_pool, _redis_pool};
    std::vector <int> _loop_eventfd;
    std::vector <std::vector<int>> _bell;
    std::array<std::mutex, loop_size> _bell_mutex;
    std::vector <std::unique_ptr<Event_loop>> _event_loop;
    std::vector <std::thread> _event_thread;
    ThreadPool _thread_pool;
};
