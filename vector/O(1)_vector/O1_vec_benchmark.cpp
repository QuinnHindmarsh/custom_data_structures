// Benchmark: O1vector<int> vs std::vector<int>, using Google Benchmark.
//
// Ops: push (empty -> N), at (random index), set (random index), pop (N -> empty).
// One benchmark iteration = N calls of one op (N = 2^21 unless built with -DBENCH_N=...).
// Every iteration uses the same random indices, for both vectors.
//
// Two families of benchmarks, both named <family>/<op>/<vector>:
//
//   bulk/...     The whole loop of N calls timed at once. "per_op" = mean time of one call.
//                No timer inside the loop, so this is the honest average.
//
//   latency/...  Every call timed on its own with rdtsc (timer overhead subtracted).
//                This is the point of O1vector: no single push or pop should be slow.
//                Columns, each the average over iterations (max = max over iterations):
//                  stddev    spread of single-call times
//                  p99.9     99.9% of calls were at least this fast
//                  worst5    mean of the 5 slowest calls in an iteration
//                  max       the single slowest call (mostly OS noise)
//                Time is the sum of the per-call times, so it reads a bit higher than bulk.
//
// Build (needs Google Benchmark installed, see README.md; -DNDEBUG turns asserts off,
// since std::vector has none):
//   g++ -std=c++23 -O2 -DNDEBUG O1_vec_benchmark.cpp -o O1_vec_benchmark.exe -lbenchmark -lshlwapi
//   (-lshlwapi is Windows only. On Linux use -lbenchmark -lpthread.)
//
// Full run, 50 repetitions, the two vectors' repetitions interleaved so slow drift (heat, turbo)
// hits both, with a warm-up first to get page faults and CPU clock ramp-up out of the way:
//   ./O1_vec_benchmark.exe --benchmark_repetitions=50 --benchmark_report_aggregates_only=true
//                          --benchmark_enable_random_interleaving=true --benchmark_min_warmup_time=0.5
// Quick check that everything runs:
//   ./O1_vec_benchmark.exe --benchmark_min_time=0.01s

#include "O(1)_vector.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>
#include <x86intrin.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef BENCH_N
#define BENCH_N (1 << 21)
#endif
constexpr std::size_t N = BENCH_N; // calls per iteration (~2 million)
constexpr std::size_t WORST_K = 5; // "worst5" = mean of this many slowest calls

// =====================================================================
// Thin wrappers so both vectors share one benchmark
// =====================================================================

struct StdVec
{
    std::vector<int> v{};
    void push(int x) { v.push_back(x); }
    int pop()
    {
        int x{v.back()};
        v.pop_back();
        return x;
    }
    int at(std::size_t i) const { return v.at(i); }   // bounds-checked, like O1vector
    void set(std::size_t i, int x) { v.at(i) = x; }    // bounds-checked, like O1vector
};

struct MyVec
{
    O1vector<int> v{};
    void push(int x) { v.push(x); }
    int pop() { return v.pop(); }
    int at(std::size_t i) const { return v.at(i); }
    void set(std::size_t i, int x) { v.set(i, x); }
};

template <typename Vec>
void fill(Vec &vec)
{
    for (std::size_t i = 0; i < N; ++i)
        vec.push(static_cast<int>(i));
}

// N random indices in [0, N), fixed seed, built once.
const std::vector<std::size_t> &indices()
{
    static const std::vector<std::size_t> idx = []
    {
        std::vector<std::size_t> v(N);
        std::mt19937 rng{0};
        std::uniform_int_distribution<std::size_t> dist{0, N - 1};
        for (std::size_t &i : v)
            i = dist(rng);
        return v;
    }();
    return idx;
}

// What one call of each op does. `i` counts 0..N-1 within an iteration.
enum class Op
{
    Push,
    At,
    Set,
    Pop
};

template <Op O, typename Vec>
inline void do_op(Vec &vec, std::size_t i, const std::vector<std::size_t> &idx)
{
    if constexpr (O == Op::Push)
        vec.push(static_cast<int>(i));
    else if constexpr (O == Op::At)
        benchmark::DoNotOptimize(vec.at(idx[i]));
    else if constexpr (O == Op::Set)
        vec.set(idx[i], static_cast<int>(i));
    else
        benchmark::DoNotOptimize(vec.pop());
}

// Puts `vec` in the state op O starts from: empty for push, N elements for the rest.
// at and set leave the vector as it was, so they keep reusing it. push and pop rebuild it.
template <Op O, typename Vec>
void prepare(std::optional<Vec> &vec)
{
    constexpr bool reuse = (O == Op::At || O == Op::Set);
    if (reuse && vec)
        return;
    vec.reset();
    vec.emplace();
    if constexpr (O != Op::Push)
        fill(*vec);
}

// =====================================================================
// bulk: mean time per call
// =====================================================================

template <Op O, typename Vec>
void BM_bulk(benchmark::State &st)
{
    const std::vector<std::size_t> &idx = indices();
    std::optional<Vec> vec;
    for (auto _ : st)
    {
        st.PauseTiming(); // setup and teardown are not timed
        prepare<O>(vec);
        st.ResumeTiming();

        for (std::size_t i = 0; i < N; ++i)
            do_op<O>(*vec, i, idx);
        benchmark::ClobberMemory();
    }
    st.counters["per_op"] = benchmark::Counter(static_cast<double>(N),
                                               benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

// =====================================================================
// latency: every call timed on its own with rdtsc
// =====================================================================

inline std::uint64_t ticks()
{
    _mm_lfence(); // stop the CPU running the timed op early/late
    std::uint64_t t{__rdtsc()};
    _mm_lfence();
    return t;
}

// How many ns one rdtsc tick is. Found by busy-waiting 200ms.
double measure_ns_per_tick()
{
    using clock = std::chrono::steady_clock;
    const auto start_time{clock::now()};
    const std::uint64_t start_ticks{ticks()};
    while (clock::now() - start_time < std::chrono::milliseconds{200})
    {
    }
    const auto end_time{clock::now()};
    const std::uint64_t end_ticks{ticks()};
    const double ns{std::chrono::duration<double, std::nano>(end_time - start_time).count()};
    return ns / static_cast<double>(end_ticks - start_ticks);
}

// Ticks taken to time "nothing". Subtracted from every per-call sample.
std::uint64_t measure_timer_overhead()
{
    std::vector<std::uint64_t> samples(100'000);
    for (std::uint64_t &s : samples)
    {
        const std::uint64_t t0{ticks()};
        const std::uint64_t t1{ticks()};
        s = t1 - t0;
    }
    std::nth_element(samples.begin(), samples.begin() + samples.size() / 2, samples.end());
    return samples[samples.size() / 2]; // median
}

double g_ns_per_tick{};
std::uint64_t g_overhead{};

// Stats of one iteration's N samples, in ns. Reorders `s`.
struct IterStats
{
    double total_ns, variance, p999_ns, worst_ns, max_ns;
};

IterStats compute_stats(std::vector<std::uint64_t> &s)
{
    const double n{static_cast<double>(s.size())};

    double sum{0};
    for (std::uint64_t x : s)
        sum += static_cast<double>(x);
    const double mean{sum / n};

    double sq{0};
    for (std::uint64_t x : s)
    {
        const double d{static_cast<double>(x) - mean};
        sq += d * d;
    }

    // Move the WORST_K largest samples to the end.
    const auto worst_begin{s.end() - static_cast<std::ptrdiff_t>(WORST_K)};
    std::nth_element(s.begin(), worst_begin, s.end());
    double worst_sum{0};
    std::uint64_t worst_max{0};
    for (auto it{worst_begin}; it != s.end(); ++it)
    {
        worst_sum += static_cast<double>(*it);
        worst_max = std::max(worst_max, *it);
    }

    // Everything before worst_begin is <= the worst K, so the 99.9th percentile is in that front part.
    const auto p999{s.begin() + static_cast<std::ptrdiff_t>((n - 1) * 0.999)};
    std::nth_element(s.begin(), p999, worst_begin);

    const double t{g_ns_per_tick};
    return {sum * t, (sq / n) * t * t, static_cast<double>(*p999) * t,
            worst_sum / WORST_K * t, static_cast<double>(worst_max) * t};
}

template <Op O, typename Vec>
void BM_latency(benchmark::State &st)
{
    const std::vector<std::size_t> &idx = indices();
    std::vector<std::uint64_t> samples(N);
    std::optional<Vec> vec;
    double variance{0}, p999{0}, worst{0}, max{0};

    // UseManualTime: the time of an iteration is what SetIterationTime says (the sum of the
    // per-call samples), so setup and stats work in the loop body are not counted.
    for (auto _ : st)
    {
        prepare<O>(vec);
        for (std::size_t i = 0; i < N; ++i)
        {
            const std::uint64_t t0{ticks()};
            do_op<O>(*vec, i, idx);
            const std::uint64_t raw{ticks() - t0};
            samples[i] = raw > g_overhead ? raw - g_overhead : 0;
        }
        const IterStats s{compute_stats(samples)};
        st.SetIterationTime(s.total_ns * 1e-9);
        variance += s.variance;
        p999 += s.p999_ns;
        worst += s.worst_ns;
        max = std::max(max, s.max_ns);
    }

    const double iters{static_cast<double>(st.iterations())};
    st.counters["stddev_ns"] = std::sqrt(variance / iters);
    st.counters["p99.9_ns"] = p999 / iters;
    st.counters["worst5_ns"] = worst / iters;
    st.counters["max_ns"] = max;
}

// =====================================================================
// Registration and main
// =====================================================================

using BenchFn = void (*)(benchmark::State &);

void add(const char *family, const char *op, const char *vec, BenchFn fn, bool manual_time)
{
    const std::string name = std::string{family} + "/" + op + "/" + vec;
    benchmark::internal::Benchmark *b = benchmark::RegisterBenchmark(name.c_str(), fn)->Unit(benchmark::kMillisecond);
    if (manual_time)
        b->UseManualTime();
}

template <Op O>
void add_op(const char *op)
{
    add("bulk", op, "std::vector", BM_bulk<O, StdVec>, false);
    add("bulk", op, "O1vector", BM_bulk<O, MyVec>, false);
    add("latency", op, "std::vector", BM_latency<O, StdVec>, true);
    add("latency", op, "O1vector", BM_latency<O, MyVec>, true);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    // Fewer OS interruptions -> cleaner worst-case numbers.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), 1 << 1); // pin to one core
#endif

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;

    g_ns_per_tick = measure_ns_per_tick();
    g_overhead = measure_timer_overhead();
    benchmark::AddCustomContext("rdtsc_overhead_ticks", std::to_string(g_overhead));

    add_op<Op::Push>("push");
    add_op<Op::At>("at");
    add_op<Op::Set>("set");
    add_op<Op::Pop>("pop");

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
