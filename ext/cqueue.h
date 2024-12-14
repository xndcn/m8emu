#pragma once

#include <memory>

namespace ext {

template<typename T>
class cqueue {
public:
    cqueue();

    void reserve(std::size_t n);
    void push(void* data, std::size_t n);
    std::size_t size();
    void peek(void* data, std::size_t n);
    void pop(void* data, std::size_t n);
    void pop(std::size_t n);

private:
    std::shared_ptr<void> q;
};

} // namespace ext
