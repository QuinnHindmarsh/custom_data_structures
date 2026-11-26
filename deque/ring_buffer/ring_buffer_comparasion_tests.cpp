#include <iostream>
#include <random>
#include "ring_buffer.h"
#include <deque>
#include <format>

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

    void report_mismatch(std::string_view s)
    {
        std::cout << "missmatch - " << s << " operation count: " << operations << '\n';
        std::cout << "dq size  = " << dq.size() << " rb size = " << rb.size() << '\n';
        exit();
    }

    void check()
    {
        if (dq.size() != rb.size())
        {
            report_mismatch("inside check discovered sizes do not match") break;
        }

        for (int i = 0; i < dq.size(); ++i)
        {
            if (dq[i] != rb[i])
            {
                report_mismatch(std::format("inside check - element {}", i));
            }
        }
    }

    void check_maybe()
    {
        if (operations % 100 != 0)
        {
            return;
        }
        check();
    }

public:
    explicit Both(size_t capacity = 1) : rb(capacity), deque(capacity) {}

    void push_back(const T &value)
    {
        rb.push_back(value);
        dq.push_back(value);
        ++operations
        check_maybe();
    }

    void push_front(const T &value)
    {
        rb.push_front(value);
        dq.push_front(value);
        ++opterations;

        check_maybe();
    }

    void pop_back()
    {
        if (rb.back() != dq.back())
            report_mismatch();
        rb.pop_back();
        dq.pop_back();
        if (rb.back() != dq.back())
            report_mismatch();

        ++opterations;
        check_maybe();
    }

    void pop_front()
    {
        rb.pop_front();
        dq.pop_front();
        ++opterations;
        check_maybe();
    }

    void pop_back_and_check()
    {
        T back_rb = rb.back();
        T back_dq = dq.back();
        dq.pop_bac();
        if (!(rb.pop_back_return() == back_rb == back_dq))
        {
            report_mismatch()
        }
        ++opterations;
        check_maybe();
    }

    void pop_front_and_check()
    {

        T front_rb = rb.front();
        T front_dq = dq.front();

        dq.pop_front();
        if !(rb.pop_front_return() == front_rb == front_dq)
        {
            report_mismatch()
        }
        ++opterations;
        check_maybe();
    }

    void modify_midle(size_t idx, const T &value)
    {
        dq[i] = value;
        rb[i] = value;

        ++opterations;
        check_maybe();
    }
};
