// Runtime benchmark: O1vector<int> vs std::vector<int>
//
// For each operation (push, at, set, pop) and each vector, this measures:
//   - mean time per op   (whole loop timed at once / N -> no timer overhead)
//   - variance + stddev  (every single op timed on its own)
//   - p99.9 and worst    (from the same per-op timings)
//
// Per-op timing uses the CPU cycle counter (rdtsc), converted to ns.
// The cost of reading the timer itself is measured and subtracted.
//
// Build & run (-O2 matters: unoptimised timings are meaningless):
//   g++ -std=c++20 -O2 O1_vec_benchmark.cpp -o O1_vec_benchmark.exe
//   ./O1_vec_benchmark.exe

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <x86intrin.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "O(1)_vector.h"

// =====================================================================
// Settings
// =====================================================================

constexpr size_t N{1 << 21}; // ops per trial (~2 million)
constexpr int TRIALS{5};     // each benchmark is repeated this many times

// =====================================================================
// Timing helpers
// =====================================================================

// Stops the compiler from deleting reads whose result is never used.
volatile int g_sink{0};

inline uint64_t ticks()
{
    _mm_lfence(); // stop the CPU running the timed op early/late
    uint64_t t{__rdtsc()};
    _mm_lfence();
    return t;
}

// How many ns one rdtsc tick is. Found by busy-waiting 200ms.
double measure_ns_per_tick()
{
    using clock = std::chrono::steady_clock;
    auto start_time{clock::now()};
    uint64_t start_ticks{ticks()};
    while (clock::now() - start_time < std::chrono::milliseconds{200})
    {
    }
    auto end_time{clock::now()};
    uint64_t end_ticks{ticks()};

    double ns{std::chrono::duration<double, std::nano>(end_time - start_time).count()};
    return ns / static_cast<double>(end_ticks - start_ticks);
}

// Ticks taken to time "nothing". Subtracted from every per-op sample.
uint64_t measure_timer_overhead()
{
    std::vector<uint64_t> samples(100000);
    for (auto &s : samples)
    {
        uint64_t t0{ticks()};
        uint64_t t1{ticks()};
        s = t1 - t0;
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2]; // median
}

double g_ns_per_tick{};
uint64_t g_overhead{};

// Collects per-op samples (in ticks) plus bulk timings across trials.
struct Result
{
    std::vector<uint64_t> samples;
    double bulk_ns_total{0};
    size_t bulk_ops{0};

    void add_sample(uint64_t raw_ticks)
    {
        samples.push_back(raw_ticks > g_overhead ? raw_ticks - g_overhead : 0);
    }
};

// Times `body(i)` for i in [0, N) as one block. Returns ns.
template <typename Body>
double time_bulk(Body body)
{
    using clock = std::chrono::steady_clock;
    auto start{clock::now()};
    for (size_t i{0}; i < N; ++i)
    {
        body(i);
    }
    auto end{clock::now()};
    return std::chrono::duration<double, std::nano>(end - start).count();
}

// Times every `body(i)` separately and stores each sample.
template <typename Body>
void time_each(Result &r, Body body)
{
    for (size_t i{0}; i < N; ++i)
    {
        uint64_t t0{ticks()};
        body(i);
        uint64_t t1{ticks()};
        r.add_sample(t1 - t0);
    }
}

// =====================================================================
// Thin wrappers so both vectors share one benchmark
// =====================================================================

struct StdVec
{
    std::vector<int> v;
    void push(int x) { v.push_back(x); }
    int pop()
    {
        int x{v.back()};
        v.pop_back();
        return x;
    }
    int at(size_t i) const { return v.at(i); }       // bounds-checked, like O1vector
    void set(size_t i, int x) { v.at(i) = x; }        // bounds-checked, like O1vector
};

struct MyVec
{
    O1vector<int> v;
    void push(int x) { v.push(x); }
    int pop() { return v.pop(); }
    int at(size_t i) const { return v.at(i); }
    void set(size_t i, int x) { v.set(i, x); }
};

// =====================================================================
// Benchmarks
// =====================================================================

struct AllResults
{
    Result push, at, set, pop;
};

std::vector<size_t> make_random_indices()
{
    std::mt19937 rng{123};
    std::uniform_int_distribution<size_t> dist{0, N - 1};
    std::vector<size_t> idx(N);
    for (auto &i : idx)
    {
        i = dist(rng);
    }
    return idx;
}

template <typename Vec>
AllResults run_benchmarks(const std::vector<size_t> &idx)
{
    AllResults r;
    for (Result *res : {&r.push, &r.at, &r.set, &r.pop})
    {
        res->samples.reserve(N * TRIALS);
    }

    for (int trial{0}; trial < TRIALS; ++trial)
    {
        // ---- bulk pass: mean time with no timer in the loop ----
        {
            Vec vec;
            r.push.bulk_ns_total += time_bulk([&](size_t i)
                                              { vec.push(static_cast<int>(i)); });
            r.at.bulk_ns_total += time_bulk([&](size_t i)
                                            { g_sink = vec.at(idx[i]); });
            r.set.bulk_ns_total += time_bulk([&](size_t i)
                                             { vec.set(idx[i], static_cast<int>(i)); });
            r.pop.bulk_ns_total += time_bulk([&](size_t)
                                             { g_sink = vec.pop(); });
            r.push.bulk_ops += N;
            r.at.bulk_ops += N;
            r.set.bulk_ops += N;
            r.pop.bulk_ops += N;
        }

        // ---- per-op pass: every op timed alone ----
        {
            Vec vec;
            time_each(r.push, [&](size_t i)
                      { vec.push(static_cast<int>(i)); });
            time_each(r.at, [&](size_t i)
                      { g_sink = vec.at(idx[i]); });
            time_each(r.set, [&](size_t i)
                      { vec.set(idx[i], static_cast<int>(i)); });
            time_each(r.pop, [&](size_t)
                      { g_sink = vec.pop(); });
        }
    }
    return r;
}

// =====================================================================
// Stats + printing
// =====================================================================

struct Stats
{
    double mean_ns;     // from bulk pass
    double variance;    // ns^2, from per-op pass
    double stddev;      // ns
    double p999_ns;     // 99.9th percentile
    double worst_ns;    // single slowest op
};

Stats compute(Result &r)
{
    std::vector<uint64_t> &s{r.samples};

    double sum{0};
    for (uint64_t x : s)
    {
        sum += static_cast<double>(x) * g_ns_per_tick;
    }
    double mean_per_op{sum / static_cast<double>(s.size())};

    double sq{0};
    for (uint64_t x : s)
    {
        double d{static_cast<double>(x) * g_ns_per_tick - mean_per_op};
        sq += d * d;
    }
    double variance{sq / static_cast<double>(s.size())};

    std::sort(s.begin(), s.end());
    size_t p999_index{static_cast<size_t>(static_cast<double>(s.size() - 1) * 0.999)};

    return {
        r.bulk_ns_total / static_cast<double>(r.bulk_ops),
        variance,
        std::sqrt(variance),
        static_cast<double>(s[p999_index]) * g_ns_per_tick,
        static_cast<double>(s.back()) * g_ns_per_tick,
    };
}

std::string fmt_ns(double ns)
{
    if (ns >= 1e6)
        return std::format("{:.2f} ms", ns / 1e6);
    if (ns >= 1e3)
        return std::format("{:.2f} us", ns / 1e3);
    return std::format("{:.2f} ns", ns);
}

void print_row(const std::string &name, const Stats &s)
{
    std::cout << std::format("  {:<12} {:>11} {:>14.1f} {:>11} {:>11} {:>11}\n",
                             name, fmt_ns(s.mean_ns), s.variance, fmt_ns(s.stddev),
                             fmt_ns(s.p999_ns), fmt_ns(s.worst_ns));
}

void print_op(const std::string &op, Result &stdr, Result &myr)
{
    Stats a{compute(stdr)};
    Stats b{compute(myr)};

    std::cout << '\n'
              << op << '\n';
    std::cout << std::format("  {:<12} {:>11} {:>14} {:>11} {:>11} {:>11}\n",
                             "", "mean", "variance ns^2", "stddev", "p99.9", "worst");
    print_row("std::vector", a);
    print_row("O1vector", b);

    double ratio{a.worst_ns / std::max(b.worst_ns, 1e-9)};
    std::cout << std::format("  worst: O1vector is {:.1f}x {} than std::vector\n",
                             ratio >= 1 ? ratio : 1 / ratio,
                             ratio >= 1 ? "better" : "worse");
}

// =====================================================================
// Main
// =====================================================================

int main()
{
#ifdef _WIN32
    // Fewer OS interruptions -> cleaner worst-case numbers.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), 1 << 1); // pin to one core
#endif

    g_ns_per_tick = measure_ns_per_tick();
    g_overhead = measure_timer_overhead();

    std::cout << std::format("N = {} ops per trial, {} trials, T = int ({} bytes)\n",
                             N, TRIALS, sizeof(int));
    std::cout << std::format("timer overhead subtracted: {} ticks ({:.1f} ns)\n",
                             g_overhead, static_cast<double>(g_overhead) * g_ns_per_tick);

    std::vector<size_t> idx{make_random_indices()};

    // Warm-up run so page faults / CPU clock ramp-up don't hit the first one.
    run_benchmarks<StdVec>(idx);

    AllResults stdr{run_benchmarks<StdVec>(idx)};
    AllResults myr{run_benchmarks<MyVec>(idx)};

    print_op("push (empty -> N)", stdr.push, myr.push);
    print_op("at (random index)", stdr.at, myr.at);
    print_op("set (random index)", stdr.set, myr.set);
    print_op("pop (N -> empty)", stdr.pop, myr.pop);

    std::cout << "\nNotes:\n"
                 "  mean    = whole loop timed / N (no timer cost)\n"
                 "  others  = each op timed alone; ~1 ns ops are near timer resolution\n"
                 "  worst   = includes OS interrupts, which hit both vectors\n";
    return 0;
}
