// Benchmark: ring_buffer vs std::deque.
//
// Element types: int32_t, int64_t, std::string (24 random bytes, too long for SSO).
// Every op is timed over N = 1,000,000 calls, PASSES times, and the MEDIAN pass is reported.
// Both containers run in the same pass, one after the other, so a noisy moment hits both.
//
// Ops (each on a fresh container, filled untimed when the op needs elements):
//   push_back / push_front    empty -> N (includes every grow)
//   push_back/front (rsv)     reserve(N), then empty -> N. reserve is inside the timed part.
//                             std::deque has no reserve, so its rows are the plain pushes again.
//   pop_back / pop_front      N -> empty
//   index                     N reads at random indices
//   front                     N reads of front()
//   back                      N reads of back()
//   take_back / take_front    read the end element (moved out), then pop it, N -> empty
//   mixed (avg)               not timed itself: the plain average of the ops above
//
// Build with optimisation, -O0 numbers are meaningless (or use the "C++23: build file (release)" task):
//   g++ -std=c++23 -O2 -DNDEBUG ring_buffer_benchmark.cpp -o ring_buffer_benchmark

#include "../../shared_utility/random.h"
#include "../../shared_utility/timer.h"
#include "ring_buffer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <vector>

constexpr std::size_t N = 1'000'000; // ops per pass
constexpr int PASSES = 20;

// ---------- ops ----------

enum Op : std::uint8_t
{
    PUSH_BACK,
    PUSH_FRONT,
    PUSH_BACK_RSV,
    PUSH_FRONT_RSV,
    POP_BACK,
    POP_FRONT,
    INDEX,
    FRONT,
    BACK,
    TAKE_BACK,
    TAKE_FRONT,
    NUM_OPS
};
constexpr const char *op_names[NUM_OPS]{"push_back", "push_front", "push_back (rsv)", "push_front (rsv)", "pop_back", "pop_front", "index",
                                        "front", "back", "take_back", "take_front"};

// "Read the element, then pop it". ring_buffer does both in one call, std::deque needs two.
template <typename T>
T take_back(ring_buffer<T> &c) { return c.pop_back_return(); }
template <typename T>
T take_front(ring_buffer<T> &c) { return c.pop_front_return(); }

template <typename T>
T take_back(std::deque<T> &c)
{
    T x = std::move(c.back());
    c.pop_back();
    return x;
}
template <typename T>
T take_front(std::deque<T> &c)
{
    T x = std::move(c.front());
    c.pop_front();
    return x;
}

// reserve if the container has it (ring_buffer), otherwise nothing (std::deque)
template <typename C>
void reserve_if_possible(C &c, std::size_t n)
{
    if constexpr (requires { c.reserve(n); })
        c.reserve(n);
}

// Tells the compiler x is used, so it can't skip the read or hoist it out of the loop.
template <typename X>
inline void keep(const X &x) { asm volatile("" : : "r,m"(x) : "memory"); }

// ---------- data: the same values and indices are fed to both containers ----------

template <typename T>
T random_value();
template <>
std::int32_t random_value<std::int32_t>() { return rng::i32(); }
template <>
std::int64_t random_value<std::int64_t>() { return rng::i64(); }
template <>
std::string random_value<std::string>() { return rng::string(24); }

template <typename T>
struct Data
{
    std::vector<T> values;          // N random values, pushed in order
    std::vector<std::size_t> index; // N random indices in [0, N)
};

template <typename T>
Data<T> make_data()
{
    Data<T> d;
    for (std::size_t i = 0; i < N; ++i)
    {
        d.values.push_back(random_value<T>());
        d.index.push_back(rng::range<std::size_t>(0, N - 1));
    }
    return d;
}

// ---------- running ----------

template <typename Fn>
double time_ns_per_op(Fn fn)
{
    Timer t;
    fn();
    return t.elapsed() * 1e9 / N;
}

template <typename C, typename T>
C filled(const Data<T> &d)
{
    C c;
    for (const T &v : d.values)
        c.push_back(v);
    return c;
}

using Times = std::vector<double>[NUM_OPS]; // ns per op, one entry per pass

// One pass of every op on container type C.
template <typename C, typename T>
void run_pass(const Data<T> &d, Times &t)
{
    {
        C c;
        t[PUSH_BACK].push_back(time_ns_per_op([&]
                                              { for (const T &v : d.values) c.push_back(v); }));
    }
    {
        C c;
        t[PUSH_FRONT].push_back(time_ns_per_op([&]
                                               { for (const T &v : d.values) c.push_front(v); }));
    }
    {
        C c;
        t[PUSH_BACK_RSV].push_back(time_ns_per_op([&]
                                                  { reserve_if_possible(c, N); for (const T &v : d.values) c.push_back(v); }));
    }
    {
        C c;
        t[PUSH_FRONT_RSV].push_back(time_ns_per_op([&]
                                                   { reserve_if_possible(c, N); for (const T &v : d.values) c.push_front(v); }));
    }
    {
        C c = filled<C>(d);
        // keep(c) stops the compiler merging N pops into one "size -= N"
        t[POP_BACK].push_back(time_ns_per_op([&]
                                             { for (std::size_t i = 0; i < N; ++i) { c.pop_back(); keep(c); } }));
    }
    {
        C c = filled<C>(d);
        t[POP_FRONT].push_back(time_ns_per_op([&]
                                              { for (std::size_t i = 0; i < N; ++i) { c.pop_front(); keep(c); } }));
    }
    {
        C c = filled<C>(d);
        t[INDEX].push_back(time_ns_per_op([&]
                                          { for (std::size_t i : d.index) keep(c[i]); }));
        t[FRONT].push_back(time_ns_per_op([&]
                                          { for (std::size_t i = 0; i < N; ++i) keep(c.front()); }));
        t[BACK].push_back(time_ns_per_op([&]
                                         { for (std::size_t i = 0; i < N; ++i) keep(c.back()); }));
    }
    {
        C c = filled<C>(d);
        t[TAKE_BACK].push_back(time_ns_per_op([&]
                                              { for (std::size_t i = 0; i < N; ++i) keep(take_back(c)); }));
    }
    {
        C c = filled<C>(d);
        t[TAKE_FRONT].push_back(time_ns_per_op([&]
                                               { for (std::size_t i = 0; i < N; ++i) keep(take_front(c)); }));
    }
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <typename T>
void bench(const char *type_name)
{
    std::printf("\n== %s ==\n", type_name);
    const Data<T> d = make_data<T>();

    Times ring{}, deque{};
    for (int p = 0; p < PASSES; ++p)
    {
        std::printf("\r  pass %d/%d   ", p + 1, PASSES);
        std::fflush(stdout);
        run_pass<ring_buffer<T>>(d, ring);
        run_pass<std::deque<T>>(d, deque);
    }

    std::printf("\r  %-17s %14s %14s %10s\n", "op (ns/op)", "ring_buffer", "std::deque", "speedup");
    double r_sum = 0, q_sum = 0;
    for (int op = 0; op < NUM_OPS; ++op)
    {
        const double r = median(ring[op]);
        const double q = median(deque[op]);
        r_sum += r;
        q_sum += q;
        std::printf("  %-17s %14.2f %14.2f %9.2fx\n", op_names[op], r, q, q / r);
    }
    const double r = r_sum / int{NUM_OPS}, q = q_sum / int{NUM_OPS};
    std::printf("  %-17s %14.2f %14.2f %9.2fx\n", "mixed (avg)", r, q, q / r);
}

int main()
{
    std::printf("N = %zu ops per pass, %d passes, median pass shown.\n", N, PASSES);
    std::printf("speedup = deque time / ring_buffer time. Above 1x means ring_buffer is faster.\n");

    bench<std::int32_t>("int32_t");
    bench<std::int64_t>("int64_t");
    bench<std::string>("std::string");
    return 0;
}
