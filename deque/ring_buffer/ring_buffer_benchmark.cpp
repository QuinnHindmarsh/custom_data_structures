// Benchmark: ring_buffer vs std::deque, using Google Benchmark.
//
// Element types: int32_t, int64_t, std::string (24 random bytes, too long for SSO).
// One benchmark iteration = N calls of one op (N = 1,000,000 unless built with -DBENCH_N=...).
// "per_op" is the time of one call. The Time column is one iteration (all N calls).
//
// Ops (each on a fresh container, filled untimed when the op needs elements):
//   push_back / push_front    empty -> N (includes every grow)
//   push_back/front (rsv)     reserve(N), then empty -> N. reserve is inside the timed part.
//                             ring_buffer only: std::deque has no reserve, so compare these
//                             with std::deque's plain push_back / push_front rows.
//   pop_back / pop_front      N -> empty
//   index                     N reads at random indices
//   front                     N reads of front()
//   back                      N reads of back()
//   take_back / take_front    read the end element (moved out), then pop it, N -> empty
//
// Benchmarks are named <op>/<element type>/<container>, so the two containers sit next to each other.
//
// Build (needs Google Benchmark installed, see README.md):
//   g++ -std=c++23 -O2 -DNDEBUG ring_buffer_benchmark.cpp -o ring_buffer_benchmark.exe -lbenchmark -lshlwapi
//   (-lshlwapi is Windows only. On Linux use -lbenchmark -lpthread.)
//
// Full run, median of 20 repetitions, the two containers' repetitions interleaved so noise hits both:
//   ./ring_buffer_benchmark.exe --benchmark_repetitions=20 --benchmark_report_aggregates_only=true
//                               --benchmark_enable_random_interleaving=true
// Quick check that everything runs:
//   ./ring_buffer_benchmark.exe --benchmark_min_time=0.01s

#include "ring_buffer.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#ifndef BENCH_N
#define BENCH_N 1'000'000
#endif
constexpr std::size_t N = BENCH_N; // calls per iteration

// ---------- "read the element, then pop it" ----------
// ring_buffer does both in one call, std::deque needs two.

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

// ---------- data: the same values and indices are fed to both containers ----------

// Fixed seed: every run of the benchmark sees the same data.
inline std::mt19937_64 &gen()
{
    static std::mt19937_64 r{42};
    return r;
}

template <typename T>
T random_value();
template <>
std::int32_t random_value<std::int32_t>() { return static_cast<std::int32_t>(gen()()); }
template <>
std::int64_t random_value<std::int64_t>() { return static_cast<std::int64_t>(gen()()); }
template <>
std::string random_value<std::string>()
{
    std::string s(24, '\0');
    for (char &ch : s)
        ch = static_cast<char>(gen()());
    return s;
}

template <typename T>
struct Data
{
    std::vector<T> values;          // N random values, pushed in order
    std::vector<std::size_t> index; // N random indices in [0, N)
};

// Built once per element type, the first time a benchmark asks for it (not timed).
template <typename T>
const Data<T> &data()
{
    static const Data<T> d = []
    {
        Data<T> d;
        std::uniform_int_distribution<std::size_t> dist{0, N - 1};
        for (std::size_t i = 0; i < N; ++i)
        {
            d.values.push_back(random_value<T>());
            d.index.push_back(dist(gen()));
        }
        return d;
    }();
    return d;
}

template <typename C, typename T>
C filled(const Data<T> &d)
{
    C c;
    for (const T &v : d.values)
        c.push_back(v);
    return c;
}

// reserve if the container has it (ring_buffer), otherwise nothing (std::deque)
template <typename C>
void reserve_if_possible(C &c, std::size_t n)
{
    if constexpr (requires { c.reserve(n); })
        c.reserve(n);
}

// Adds a "per_op" column: time of one call, as each iteration does N calls.
void report_per_op(benchmark::State &st)
{
    st.counters["per_op"] = benchmark::Counter(static_cast<double>(N),
                                               benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

// ---------- benchmarks ----------
// Setup and teardown (filling, destroying) run inside PauseTiming / ResumeTiming, so they are not timed.
// That pause costs well under a microsecond, which is nothing next to N calls.

template <typename C, typename T, bool Front, bool Reserve>
void BM_push(benchmark::State &st)
{
    const Data<T> &d = data<T>();
    std::optional<C> c;
    for (auto _ : st)
    {
        st.PauseTiming();
        c.emplace();
        st.ResumeTiming();

        if constexpr (Reserve)
            reserve_if_possible(*c, N);
        for (const T &v : d.values)
        {
            if constexpr (Front)
                c->push_front(v);
            else
                c->push_back(v);
        }
        benchmark::ClobberMemory();

        st.PauseTiming();
        c.reset(); // destroying N elements is not part of push
        st.ResumeTiming();
    }
    report_per_op(st);
}

template <typename C, typename T, bool Front>
void BM_pop(benchmark::State &st)
{
    const Data<T> &d = data<T>();
    std::optional<C> c;
    for (auto _ : st)
    {
        st.PauseTiming();
        c.emplace(filled<C>(d));
        st.ResumeTiming();

        for (std::size_t i = 0; i < N; ++i)
        {
            if constexpr (Front)
                c->pop_front();
            else
                c->pop_back();
            benchmark::DoNotOptimize(*c); // stops the compiler merging N pops into one "size -= N"
        }

        st.PauseTiming();
        c.reset();
        st.ResumeTiming();
    }
    report_per_op(st);
}

template <typename C, typename T>
void BM_index(benchmark::State &st)
{
    const Data<T> &d = data<T>();
    C c = filled<C>(d); // read-only, so one fill is enough for every iteration
    for (auto _ : st)
        for (std::size_t i : d.index)
            benchmark::DoNotOptimize(c[i]);
    report_per_op(st);
}

template <typename C, typename T>
void BM_front(benchmark::State &st)
{
    C c = filled<C>(data<T>());
    for (auto _ : st)
        for (std::size_t i = 0; i < N; ++i)
            benchmark::DoNotOptimize(c.front());
    report_per_op(st);
}

template <typename C, typename T>
void BM_back(benchmark::State &st)
{
    C c = filled<C>(data<T>());
    for (auto _ : st)
        for (std::size_t i = 0; i < N; ++i)
            benchmark::DoNotOptimize(c.back());
    report_per_op(st);
}

template <typename C, typename T, bool Front>
void BM_take(benchmark::State &st)
{
    const Data<T> &d = data<T>();
    std::optional<C> c;
    for (auto _ : st)
    {
        st.PauseTiming();
        c.emplace(filled<C>(d));
        st.ResumeTiming();

        for (std::size_t i = 0; i < N; ++i)
        {
            if constexpr (Front)
                benchmark::DoNotOptimize(take_front(*c));
            else
                benchmark::DoNotOptimize(take_back(*c));
        }

        st.PauseTiming();
        c.reset();
        st.ResumeTiming();
    }
    report_per_op(st);
}

// ---------- registration ----------

using BenchFn = void (*)(benchmark::State &);

void add(const std::string &op, const char *type_name, const char *container, BenchFn fn)
{
    const std::string name = op + "/" + type_name + "/" + container;
    benchmark::RegisterBenchmark(name.c_str(), fn)->Unit(benchmark::kMillisecond);
}

// Registers every op for element type T, ring_buffer then std::deque for each op.
template <typename T>
void add_all(const char *t)
{
    using R = ring_buffer<T>;
    using D = std::deque<T>;

    add("push_back", t, "ring_buffer", BM_push<R, T, false, false>);
    add("push_back", t, "std::deque", BM_push<D, T, false, false>);
    add("push_front", t, "ring_buffer", BM_push<R, T, true, false>);
    add("push_front", t, "std::deque", BM_push<D, T, true, false>);
    add("push_back_rsv", t, "ring_buffer", BM_push<R, T, false, true>);
    add("push_front_rsv", t, "ring_buffer", BM_push<R, T, true, true>);
    add("pop_back", t, "ring_buffer", BM_pop<R, T, false>);
    add("pop_back", t, "std::deque", BM_pop<D, T, false>);
    add("pop_front", t, "ring_buffer", BM_pop<R, T, true>);
    add("pop_front", t, "std::deque", BM_pop<D, T, true>);
    add("index", t, "ring_buffer", BM_index<R, T>);
    add("index", t, "std::deque", BM_index<D, T>);
    add("front", t, "ring_buffer", BM_front<R, T>);
    add("front", t, "std::deque", BM_front<D, T>);
    add("back", t, "ring_buffer", BM_back<R, T>);
    add("back", t, "std::deque", BM_back<D, T>);
    add("take_back", t, "ring_buffer", BM_take<R, T, false>);
    add("take_back", t, "std::deque", BM_take<D, T, false>);
    add("take_front", t, "ring_buffer", BM_take<R, T, true>);
    add("take_front", t, "std::deque", BM_take<D, T, true>);
}

int main(int argc, char **argv)
{
    add_all<std::int32_t>("int32_t");
    add_all<std::int64_t>("int64_t");
    add_all<std::string>("std::string");

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
