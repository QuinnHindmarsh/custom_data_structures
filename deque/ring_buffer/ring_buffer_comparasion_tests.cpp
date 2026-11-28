#include <iostream>
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

    void check() { compare(rb, "check"); }

    // Any ring_buffer (rb itself, or a copy of it) must hold exactly what dq holds
    void compare(const ring_buffer<T> &r, std::string_view where)
    {
        if (dq.size() != r.size())
        {
            report_mismatch(std::format("{} - sizes do not match", where));
        }

        for (size_t i = 0; i < dq.size(); ++i)
        {
            if (dq[i] != r[i])
            {
                report_mismatch(std::format("{} - element {}", where, i));
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

    // std::deque has no reserve, so only rb changes. reserve moves every element, so check them all now.
    void reserve(size_t n)
    {
        size_t size_before = rb.size();
        rb.reserve(n);
        if (rb.size() != size_before)
            report_mismatch("reserve - size changed");

        ++operations;
        check();
    }

    // The copy must match, and must be independent: changing it must not touch rb.
    void copy_construct(const T &value)
    {
        ring_buffer<T> copy(rb);
        compare(copy, "copy_construct - fresh copy");

        // copy has the same capacity as rb, so if rb is full this push makes the copy grow
        copy.push_back(value);
        copy.push_front(value);
        if (copy.size() != dq.size() + 2 || copy.front() != value || copy.back() != value)
            report_mismatch("copy_construct - copy after pushes");
        for (size_t i = 0; i < dq.size(); ++i)
        {
            if (copy[i + 1] != dq[i])
                report_mismatch(std::format("copy_construct - copy element {} after pushes", i));
        }

        ++operations;
        check(); // rb untouched
    }

    // Assign over a buffer that already holds something, plus self-assignment
    void copy_assign(const T &value)
    {
        ring_buffer<T> other;
        other.push_back(value);
        other.push_front(value);
        other = rb;
        compare(other, "copy_assign - assigned copy");

        ring_buffer<T> &self = rb;
        rb = self;

        ++operations;
        check();
    }

    // Swap rb for a copy of itself, so the next operations run on a copy-constructed buffer
    void replace_with_copy()
    {
        rb = ring_buffer<T>(rb);

        ++operations;
        check();
    }
};

// Counts live objects. Every construction adds 1, every destruction removes 1,
// so after all containers are gone the count must be back to 0.
// Not trivially copyable, so it also exercises the uninitialized_move / destroy path in grow().
struct Tracked
{
    static inline long long live{0};
    int64_t v{};

    Tracked(int64_t x = 0) : v{x} { ++live; }
    Tracked(const Tracked &o) : v{o.v} { ++live; }
    Tracked(Tracked &&o) noexcept : v{o.v} { ++live; }
    Tracked &operator=(const Tracked &) = default;
    Tracked &operator=(Tracked &&) = default;
    ~Tracked() { --live; }

    bool operator==(const Tracked &) const = default;
};

void fail(std::string_view msg)
{
    std::cout << "FAIL - " << msg << '\n';
    std::exit(1);
}

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
             size_t idx = random::range<size_t>(0, b.size() - 1);
             b.modify_midle(idx, gen());
         }},

        // up to 2x the current size, so some calls are below capacity (must do nothing) and some grow
        {"reserve", [](Both<T> &b)
         { b.reserve(random::range<size_t>(0, b.size() * 2 + 8)); }},

        {"copy_construct", [gen](Both<T> &b)
         { b.copy_construct(gen()); }},

        {"copy_assign", [gen](Both<T> &b)
         { b.copy_assign(gen()); }},

        {"replace_with_copy", [](Both<T> &b)
         { b.replace_with_copy(); }},
    };
}

// M rounds of N random operations, each round on a fresh pair with a random starting capacity
template <typename T, typename Gen>
void run_random(Gen gen, int N, int M)
{
    auto ops = make_operations<T>(gen);

    for (int i{0}; i < M; ++i)
    {
        Both<T> both(random::range<size_t>(0, static_cast<size_t>(N + 1)));
        for (int j{0}; j < N; ++j)
        {
            auto it = std::next(ops.begin(), random::range<std::ptrdiff_t>(0, std::ssize(ops) - 1));
            it->second(both); // call it
        }
        both.check();
    }
}

void test_int32(int N, int M) { run_random<int32_t>(random::i32, N, M); }
void test_uint64(int N, int M) { run_random<uint64_t>(random::u64, N, M); }

void test_string(int N, int M)
{
    run_random<std::string>([]
                            { return random::string(random::range<size_t>(0, 32)); },
                            N, M);
}

// Every starting capacity, including 0 and non powers of 2, gives an empty buffer that works
void test_constructor()
{
    for (size_t cap : {0uz, 1uz, 2uz, 3uz, 7uz, 8uz, 9uz, 1000uz})
    {
        ring_buffer<int32_t> rb(cap);
        if (!rb.empty() || rb.size() != 0)
            fail(std::format("constructor({}) - not empty", cap));

        // enough pushes to wrap and grow past the starting capacity
        for (int32_t i{0}; i < 20; ++i)
        {
            rb.push_back(i);
            rb.push_front(-i);
        }
        if (rb.size() != 40 || rb.front() != -19 || rb.back() != 19)
            fail(std::format("constructor({}) - wrong contents after pushes", cap));
    }

    ring_buffer<std::string> s;
    if (!s.empty())
        fail("default constructor - not empty");
    s.push_back("a long string that does not fit in SSO");
    if (s.size() != 1 || s.front() != "a long string that does not fit in SSO")
        fail("default constructor - wrong contents after push");
}

// {a, b, c} holds exactly those elements, in order, and still works after more pushes
void test_initializer_list()
{
    ring_buffer<int32_t> rb{10, 20, 30};
    if (rb.size() != 3 || rb[0] != 10 || rb[1] != 20 || rb[2] != 30)
        fail(std::format("initializer_list - int, expected size 3 got {}", rb.size()));
    rb.push_front(5);
    rb.push_back(40); // grows past the starting capacity of 4
    if (rb.size() != 5 || rb.front() != 5 || rb.back() != 40 || rb[1] != 10)
        fail("initializer_list - int, wrong contents after pushes");

    // braces mean "these elements", brackets mean "this capacity"
    ring_buffer<int32_t> one{5};
    ring_buffer<int32_t> cap(5);
    if (one.size() != 1 || one.front() != 5)
        fail("initializer_list - {5} should hold one element");
    if (!cap.empty())
        fail("initializer_list - (5) should be empty");

    ring_buffer<int32_t> none{std::initializer_list<int32_t>{}};
    if (!none.empty())
        fail("initializer_list - empty list should be empty");

    // non trivial type: memory starts uninitialised, so elements must be constructed, not assigned
    std::string big(40, 'x'); // too long for SSO, so a bad copy is likely to crash
    ring_buffer<std::string> s{big, "b", big};
    if (s.size() != 3 || s[0] != big || s[1] != "b" || s[2] != big)
        fail("initializer_list - string, wrong contents");

    // each element constructed once and destroyed once
    {
        ring_buffer<Tracked> t{Tracked{1}, Tracked{2}, Tracked{3}};
        if (Tracked::live != 3)
            fail(std::format("initializer_list - expected 3 live Tracked, got {}", Tracked::live));
    }
    if (Tracked::live != 0)
        fail(std::format("initializer_list - left {} live Tracked", Tracked::live));
}

// Every element must be destroyed exactly once: not leaked, not destroyed twice
void test_destructor(int N, int M)
{
    // simple case: wrapped contents (pushed on both ends) after several grows
    {
        ring_buffer<Tracked> rb;
        for (int i{0}; i < 100; ++i)
        {
            rb.push_back(Tracked{i});
            rb.push_front(Tracked{-i});
        }
        for (int i{0}; i < 30; ++i)
        {
            rb.pop_back();
            rb.pop_front();
        }
        if (Tracked::live != 140)
            fail(std::format("destructor - expected 140 live before destruction, got {}", Tracked::live));
    }
    if (Tracked::live != 0)
        fail(std::format("destructor - simple case left {} live objects", Tracked::live));

    // random ops, including reserve and all the copies
    for (int i{0}; i < M; ++i)
    {
        run_random<Tracked>([]
                            { return Tracked{random::i64()}; },
                            N, 1);
        if (Tracked::live != 0)
            fail(std::format("destructor - round {} left {} live objects", i, Tracked::live));
    }
}

int main()
{
    test_constructor();
    std::cout << "test_constructor passed\n";
    test_initializer_list();
    std::cout << "test_initializer_list passed\n";
    test_destructor(1'000, 100);
    std::cout << "test_destructor passed\n";
    test_int32(10'000, 100);
    std::cout << "test_int32 passed\n";
    test_uint64(10'000, 100);
    std::cout << "test_uint64 passed\n";
    test_string(1'000, 100);
    std::cout << "test_string passed\n";
}
