#include "timer.h"

namespace m8 {

Timer::Timer()
{
    running = true;
    thread = std::thread([this] {
        auto now = std::chrono::steady_clock::now();
        while (running) {
            std::unique_lock lock(mutex);
            if (!enabled) {
                wakeup.wait(lock, [this]() { return enabled || !running; });
                now = std::chrono::steady_clock::now();
            }
            if (enabled) {
                std::this_thread::sleep_until(now + interval);
                now = std::chrono::steady_clock::now();
                callback(*this);
                if (oneshot) {
                    Stop();
                }
            }
        }
    });
}

Timer::~Timer()
{
    if (running) {
        running = false;
        wakeup.notify_all();
        thread.join();
    }
}

void Timer::SetOneshot(bool oneshot)
{
    std::unique_lock lock(mutex);
    this->oneshot = oneshot;
}

void Timer::SetInterval(std::chrono::microseconds interval, std::function<void(Timer&)> callback)
{
    std::unique_lock lock(mutex);
    this->callback = callback;
    this->interval = interval;
}

void Timer::Start()
{
    std::unique_lock lock(mutex);
    if (!enabled) {
        enabled = true;
        wakeup.notify_all();
    }
}

void Timer::Stop()
{
    enabled = false;
}

} // namespace m8
