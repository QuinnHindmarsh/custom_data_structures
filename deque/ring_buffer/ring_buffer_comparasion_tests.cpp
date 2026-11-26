#include <iostream>
#include <random>
#include "ring-bugger-deque"
#include <deque>

bool throws(const auto &fn)
{
    try
    {
        fn();
    }
    catch (...)
    {
        return true;
    }
    return false;
}

template <typename T>
class Both
{
private:
    ring_buffer<T> rb;
    deque<T> dq;
    int operations{0};

    void check();

    void check_maybe();

public:
    explicit Both(size_t capacity = 1) : rb(capacity), deque(capacity) {}

    void push_back(const T &value)
    {
        rb.push_back(value);
        dq.push_back(value);
        ++operations
    }

    void push_front(const T &value)
    {
        rb.push_front(value);
        dq.push_front(value);
    }
};
