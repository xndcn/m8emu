#pragma once

#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <condition_variable>

namespace m8 {

class Timer {
public:
    Timer();
    ~Timer();
    void SetInterval(std::chrono::microseconds interval, std::function<void(Timer&)> callback);
    void SetOneshot(bool oneshot);
    void Start();
    void Stop();

private:
    std::atomic<bool> running{false};
    std::atomic<bool> enabled{false};
    bool oneshot = false;
    std::function<void(Timer&)> callback;
    std::chrono::microseconds interval;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wakeup;
};

} // namespace m8
