#include <iostream>
#include <random>
#include <cstdlib>
#include "ring_buffer.h"
#include <deque>
#include <format>
#include <map>
#include <functional>
#include <string>
#include "../../shared_utility/random.h"


template <typename T>
class Both
{
private:
    ring_buffer<T> rb;
    std::deque<T> dq;
    int operations{0};

    void report_mismatch(std::string_view s)
    {
        std::cout << "missmatch - " << s << " operation count: " << operations << '\n';
        std::cout << "dq size  = " << dq.size() << " rb size = " << rb.size() << '\n';
        std::exit(1);
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
    // dq starts empty: std::deque(n) would create n default elements
    explicit Both(size_t capacity = 1) : rb(capacity), dq{} {}

    size_t size() const { return dq.size(); }

    void check()
    {
        if (dq.size() != rb.size())
        {
            report_mismatch("inside check discovered sizes do not match");
        }

        for (size_t i = 0; i < dq.size(); ++i)
        {
            if (dq[i] != rb[i])
            {
                report_mismatch(std::format("inside check - element {}", i));
            }
        }
    }

    void push_back(const T &value)
    {

        rb.push_back(value);
        dq.push_back(value);
        ++operations;
        check_maybe();
    }

    void push_front(const T &value)
    {

        rb.push_front(value);
        dq.push_front(value);
        ++operations;
        check_maybe();
    }

    void pop_back()
    {
        if (dq.empty())


        if (rb.back() != dq.back())
            report_mismatch("pop_back - back before pop");
        rb.pop_back();
        dq.pop_back();
        // back() on an empty container is invalid, so only compare if something is left
        if (!dq.empty() && rb.back() != dq.back())
            report_mismatch("pop_back - back after pop");

        ++operations;
        check_maybe();
    }

    void pop_front()
    {


        if (rb.front() != dq.front())
            report_mismatch("pop_front - front before pop");
        rb.pop_front();
        dq.pop_front();
        if (!dq.empty() && rb.front() != dq.front())
            report_mismatch("pop_front - front after pop");

        ++operations;
        check_maybe();
    }

    void pop_back_and_check()
    {

        T back_rb = rb.back();
        T back_dq = dq.back();
        dq.pop_back();
        T popped = rb.pop_back_return();
        if (back_rb != back_dq || popped != back_dq)
        {
            report_mismatch("pop_back_and_check");
        }
        ++operations;
        check_maybe();
    }

    void pop_front_and_check()
    {


        T front_rb = rb.front();
        T front_dq = dq.front();
        dq.pop_front();
        T popped = rb.pop_front_return();
        if (front_rb != front_dq || popped != front_dq)
        {
            report_mismatch("pop_front_and_check");
        }
        ++operations;
        check_maybe();
    }

    void modify_midle(size_t idx, const T &value)
    {
        dq[idx] = value;
        rb[idx] = value;

        ++operations;
        check_maybe();
    }
};

// Every operation gets the same signature so they can all live in one map.
template <typename T>
using Op = std::function<void(Both<T> &)>;

// gen: any function that takes nothing and returns a T, e.g. random::get_i32
template <typename T, typename Gen>
std::map<std::string, Op<T>> make_operations(Gen gen)
{
    return {
        {"push_back", [gen](Both<T> &b)
         { b.push_back(gen()); }},

        {"push_front", [gen](Both<T> &b)
         { b.push_front(gen()); }},

        {"pop_back", [](Both<T> &b)
         { if (b.size() > 0) b.pop_back(); }},

        {"pop_front", [](Both<T> &b)
         { if (b.size() > 0) b.pop_front(); }},

        {"pop_back_and_check", [](Both<T> &b)
         { if (b.size() > 0) b.pop_back_and_check(); }},

        {"pop_front_and_check", [](Both<T> &b)
         { if (b.size() > 0) b.pop_front_and_check(); }},

        {"modify_midle", [gen](Both<T> &b)
         {
             if (b.size() == 0)
                 return;
             size_t idx = random::get_range<size_t>(0, b.size() - 1);
             b.modify_midle(idx, gen());
         }},
    };
}

void test_int32(int N)
{
    Both<int32_t> both;
    auto ops = make_operations<int32_t>(random::i32);

    for (int i{0}; i < N; ++i)
    {
        auto it = std::next(ops.begin(), random::range<std::ptrdiff_t>(0, std::ssize(ops) - 1));
        it->second(both); // call it
    }
    both.check();
}

void test_uint64(int N)
{
    Both<uint64_t> both;
    auto ops = make_operations<uint64_t>(random::u64);

    for (int i{0}; i < N; ++i)
    {
        auto it = std::next(ops.begin(), random::range<std::ptrdiff_t>(0, std::ssize(ops) - 1));
        it->second(both); // call it
    }
    both.check();
}

void test_string(int N)
{
    Both<std::string> both;
    auto gen{
        []()
        {
            return random::string(random::range<size_t>(0, 32));
        }};
    auto ops = make_operations<std::string>(gen);

    for (int i{0}; i < N; ++i)
    {
        auto it = std::next(ops.begin(), random::range<std::ptrdiff_t>(0, std::ssize(ops) - 1));
        it->second(both); // call it
    }
    both.check();
}

int main()
{
    test_int32(1'000'000);
    std::cout << "test_int32 passed\n";
    test_uint64(1'000'000);
    std::cout << "test_uint64 passed\n";
    test_string(100'000);
    std::cout << "test_string passed\n";
}
