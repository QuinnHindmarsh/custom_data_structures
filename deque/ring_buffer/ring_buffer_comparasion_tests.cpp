#include <iostream>
#include <cstdlib>
#include "ring_buffer.h"
#include <deque>
#include <format>
#include <map>
#include <functional>
#include <string>
#include <stdexcept>
#include <vector>
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

    // Swap rb for a copy of itself, so the next operations run on a copy-constructed buffer.
    // The right side is a temporary, so this now goes through move assignment.
    void replace_with_copy()
    {
        rb = ring_buffer<T>(rb);

        ++operations;
        check();
    }

    // Move rb out and back. The moved-from buffer must be empty and still usable.
    void move_construct(const T &value)
    {
        ring_buffer<T> moved(std::move(rb));
        compare(moved, "move_construct - moved to");
        if (!rb.empty() || rb.capacity() != 0)
            report_mismatch("move_construct - moved from not empty");

        // moved-from must still work: push grows it from capacity 0
        rb.push_back(value);
        rb.push_front(value);
        if (rb.size() != 2 || rb.front() != value || rb.back() != value)
            report_mismatch("move_construct - moved from not usable");

        rb = std::move(moved); // move assign over a non-empty buffer
        ++operations;
        check();
    }

    // Move assign over a buffer with contents, plus self move
    void move_assign(const T &value)
    {
        ring_buffer<T> other;
        other.push_back(value);
        other = std::move(rb);
        compare(other, "move_assign - assigned to");

        rb = std::move(other);
        ring_buffer<T> &self = rb;
        rb = std::move(self); // must not lose the contents

        ++operations;
        check();
    }

    // Read the same index with at() and [], and check at() throws one past the end
    void at(size_t idx)
    {
        const ring_buffer<T> &crb = rb;
        if (rb.at(idx) != dq.at(idx) || crb.at(idx) != dq[idx] || &rb.at(idx) != &rb[idx])
            report_mismatch("at - value");

        bool threw = false;
        try
        {
            (void)rb.at(rb.size());
        }
        catch (const std::out_of_range &)
        {
            threw = true;
        }
        if (!threw)
            report_mismatch("at - no throw at size()");
        ++operations;
    }

    // emplace returns a reference to the new element
    void emplace_back(const T &value)
    {
        T &r = rb.emplace_back(value);
        dq.emplace_back(value);
        if (&r != &rb.back() || r != value)
            report_mismatch("emplace_back - returned reference");
        ++operations;
        check_maybe();
    }

    void emplace_front(const T &value)
    {
        T &r = rb.emplace_front(value);
        dq.emplace_front(value);
        if (&r != &rb.front() || r != value)
            report_mismatch("emplace_front - returned reference");
        ++operations;
        check_maybe();
    }

    // Push an element of rb into rb itself. When rb is full this grows, and the argument
    // points into the old storage that grow frees.
    void push_self_back()
    {
        T front = dq.front();
        rb.push_back(rb.front());
        dq.push_back(front);
        ++operations;
        check_maybe();
    }

    void push_self_front()
    {
        T back = dq.back();
        rb.push_front(rb.back());
        dq.push_front(back);
        ++operations;
        check_maybe();
    }

    void clear()
    {
        size_t cap = rb.capacity();
        rb.clear();
        dq.clear();
        if (rb.capacity() != cap)
            report_mismatch("clear - capacity changed");
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

// gen: any function that takes nothing and returns a T, e.g. rng::get_i32
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
             size_t idx = rng::range<size_t>(0, b.size() - 1);
             b.modify_midle(idx, gen());
         }},

        // up to 2x the current size, so some calls are below capacity (must do nothing) and some grow
        {"reserve", [](Both<T> &b)
         { b.reserve(rng::range<size_t>(0, b.size() * 2 + 8)); }},

        {"copy_construct", [gen](Both<T> &b)
         { b.copy_construct(gen()); }},

        {"copy_assign", [gen](Both<T> &b)
         { b.copy_assign(gen()); }},

        {"replace_with_copy", [](Both<T> &b)
         { b.replace_with_copy(); }},

        {"move_construct", [gen](Both<T> &b)
         { b.move_construct(gen()); }},

        {"move_assign", [gen](Both<T> &b)
         { b.move_assign(gen()); }},

        {"at", [](Both<T> &b)
         { if (b.size() > 0) b.at(rng::range<size_t>(0, b.size() - 1)); }},

        {"emplace_back", [gen](Both<T> &b)
         { b.emplace_back(gen()); }},

        {"emplace_front", [gen](Both<T> &b)
         { b.emplace_front(gen()); }},

        {"push_self_back", [](Both<T> &b)
         { if (b.size() > 0) b.push_self_back(); }},

        {"push_self_front", [](Both<T> &b)
         { if (b.size() > 0) b.push_self_front(); }},

        // rare, otherwise buffers never get big
        {"clear", [](Both<T> &b)
         { if (rng::range<int>(0, 49) == 0) b.clear(); }},
    };
}

// M rounds of N random operations, each round on a fresh pair with a random starting capacity
template <typename T, typename Gen>
void run_random(Gen gen, int N, int M)
{
    auto ops = make_operations<T>(gen);

    for (int i{0}; i < M; ++i)
    {
        Both<T> both(rng::range<size_t>(0, static_cast<size_t>(N + 1)));
        for (int j{0}; j < N; ++j)
        {
            auto it = std::next(ops.begin(), rng::range<std::ptrdiff_t>(0, std::ssize(ops) - 1));
            it->second(both); // call it
        }
        both.check();
    }
}

void test_int32(int N, int M) { run_random<int32_t>(rng::i32, N, M); }
void test_uint64(int N, int M) { run_random<uint64_t>(rng::u64, N, M); }

void test_string(int N, int M)
{
    run_random<std::string>([]
                            { return rng::string(rng::range<size_t>(0, 32)); },
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
                            { return Tracked{rng::i64()}; },
                            N, 1);
        if (Tracked::live != 0)
            fail(std::format("destructor - round {} left {} live objects", i, Tracked::live));
    }
}

// Compile time checks: moves exist and are noexcept, so std::vector<ring_buffer> moves instead of copying on grow
static_assert(std::is_nothrow_move_constructible_v<ring_buffer<std::string>>);
static_assert(std::is_nothrow_move_assignable_v<ring_buffer<std::string>>);
static_assert(std::is_nothrow_swappable_v<ring_buffer<std::string>>);
static_assert(std::is_same_v<ring_buffer<int>::value_type, int>);

// Moving must steal the buffer: no element is copied or moved, and the source ends up empty
void test_move()
{
    {
        ring_buffer<Tracked> a;
        for (int i{0}; i < 10; ++i)
            a.push_back(Tracked{i});
        const Tracked *first = &a.front();

        ring_buffer<Tracked> b(std::move(a));
        if (&b.front() != first || b.size() != 10 || !a.empty() || Tracked::live != 10)
            fail("move ctor - did not steal the buffer");

        ring_buffer<Tracked> c{Tracked{99}};
        c = std::move(b); // c's old element must be destroyed
        if (&c.front() != first || c.size() != 10 || !b.empty() || Tracked::live != 10)
            fail("move assign - did not steal the buffer");

        // copying a moved-from buffer gives an empty, usable buffer
        ring_buffer<Tracked> d(a);
        d.push_back(Tracked{1});
        if (d.size() != 1)
            fail("copy of moved-from - not usable");
    }
    if (Tracked::live != 0)
        fail(std::format("move - left {} live Tracked", Tracked::live));

    // the reason noexcept matters: std::vector moves ring_buffers when it grows, so element addresses survive
    std::vector<ring_buffer<int>> v;
    v.emplace_back(ring_buffer<int>{1, 2, 3});
    const int *p = &v[0].front();
    for (int i{0}; i < 100; ++i)
        v.emplace_back();
    if (&v[0].front() != p)
        fail("vector<ring_buffer> - copied instead of moved on grow");

    // free swap is found by `using std::swap; swap(a, b)`
    ring_buffer<int> x{1, 2}, y{3};
    using std::swap;
    swap(x, y);
    if (x.size() != 1 || x[0] != 3 || y.size() != 2 || y[1] != 2)
        fail("swap - wrong contents");
}

void test_at_emplace_clear()
{
    ring_buffer<std::pair<int, std::string>> rb;
    auto &e = rb.emplace_back(1, "one"); // built in place from the pair's constructor arguments
    rb.emplace_front(0, "zero");
    if (e.second != "one" || rb.at(0).first != 0 || rb.at(1).second != "one")
        fail("emplace - wrong contents");

    bool threw = false;
    try
    {
        (void)rb.at(2);
    }
    catch (const std::out_of_range &)
    {
        threw = true;
    }
    if (!threw)
        fail("at - did not throw");

    rb.clear();
    if (!rb.empty() || rb.capacity() != 2)
        fail("clear - not empty or capacity changed");
    rb.emplace_back(5, "five");
    if (rb.size() != 1 || rb.front().first != 5)
        fail("clear - not usable after");

    ring_buffer<int> a{1, 2, 3};
    ring_buffer<int> b(16);
    b.push_front(1);
    b.push_back(2);
    b.push_back(3);
    if (a != b || a == ring_buffer<int>{1, 2})
        fail("operator== - wrong result");
}

// push_back(rb.front()) on a full buffer: the argument lives in storage grow() frees
void test_self_push()
{
    for (int round{0}; round < 2; ++round)
    {
        ring_buffer<std::string> rb(1);
        rb.push_back(std::string(40, 'a'));
        for (int i{0}; i < 10; ++i)
        {
            if (round == 0)
                rb.push_back(rb.front());
            else
                rb.push_front(rb.back());
        }
        for (size_t i{0}; i < rb.size(); ++i)
            if (rb[i] != std::string(40, 'a'))
                fail("self push - wrong contents");
    }
}

int main()
{
    test_move();
    std::cout << "test_move passed\n";
    test_at_emplace_clear();
    std::cout << "test_at_emplace_clear passed\n";
    test_self_push();
    std::cout << "test_self_push passed\n";
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
