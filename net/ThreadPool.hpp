#pragma once
#include <iostream>
#include <functional>
#include <mutex>
#include <vector>
#include <queue>
#include <thread>
#include <condition_variable>

using task_t = std::function<void()>;
const size_t thread_num = 16;

class ThreadPool {
public:
    explicit ThreadPool(size_t num = thread_num) : _stop(false) {
        for (int i = 0; i < num; i++) {
            _workers.emplace_back([this] () {
                while (true) {
                    task_t task;
                    {
                        std::unique_lock<std::mutex> lock(_mutex);
                        _cv.wait(lock, [this] () {
                            return _stop || !_tasks.empty();
                        });

                        if (_stop && _tasks.empty()) return ;

                        task = std::move(_tasks.front());
                        _tasks.pop();
                    }
                    try {
                        task();
                    } catch (const std::exception& e) {
                        std::cerr << "ThreadPool task exception: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "ThreadPool task unknown exception" << std::endl;
                    }
                }
            });
        }
    }

    bool submit(task_t task) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stop) return false;

            _tasks.push(std::move(task));
        }
        _cv.notify_one();
        return true;
    }


    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stop = true;
        }
        _cv.notify_all();
        for (auto &worker : _workers) {
            worker.join();
        }
    }

private:
    std::condition_variable _cv;
    std::mutex _mutex;
    std::queue <task_t> _tasks;
    std::vector <std::thread> _workers;
    std::atomic<bool> _stop;
};