// Runtime benchmark: O1vector<int> vs std::vector<int>
//
// Each operation (push, at, set, pop) is measured over RUNS separate runs,
// each run doing N ops on a fresh vector. std::vector and O1vector take
// turns going first, and every run uses new random indices.
//
// Per run, for each operation:
//   - mean          whole loop timed at once / N (no timer cost)
//   - variance      every op timed on its own (rdtsc), ns^2
//   - p99.9         99.9% of ops were at least this fast
//   - worst-5 avg   mean of the 5 slowest ops in that run
//   - max           the single slowest op in that run
//
// The table shows each of those averaged over all runs (max = overall max).
// "+-" on the mean is a 95% confidence range: the true mean is very likely
// inside it. If two vectors' ranges overlap, the difference may be noise.
//
// Build & run (-O2 matters: unoptimised timings are meaningless;
// -DNDEBUG turns asserts off, since std::vector has none):
//   g++ -std=c++20 -O2 -DNDEBUG -static O1_vec_benchmark.cpp -o O1_vec_benchmark.exe
//   ./O1_vec_benchmark.exe
// Or in VS Code: Terminal > Run Task > "run O1_vec_benchmark (release)"

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

constexpr size_t N{1 << 21};   // ops per run (~2 million)
constexpr int RUNS{50};        // separate runs per vector
constexpr int WARMUP_RUNS{2};  // thrown away: page faults, CPU clock ramp-up
constexpr size_t WORST_K{5};  // "worst-5 avg" = mean of this many slowest ops

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

// Times `body(i)` for i in [0, N) as one block. Returns ns per op.
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
    return std::chrono::duration<double, std::nano>(end - start).count() / N;
}

// Times every `body(i)` separately into `samples` (ticks, overhead removed).
template <typename Body>
void time_each(std::vector<uint64_t> &samples, Body body)
{
    for (size_t i{0}; i < N; ++i)
    {
        uint64_t t0{ticks()};
        body(i);
        uint64_t t1{ticks()};
        uint64_t raw{t1 - t0};
        samples[i] = raw > g_overhead ? raw - g_overhead : 0;
    }
}

// =====================================================================
// Stats for one run of one operation
// =====================================================================

struct RunStats
{
    double mean_ns;   // bulk pass
    double variance;  // ns^2
    double p999_ns;
    double worst_ns;
    double max_ns;
};

// Reorders `s` (that's fine, it's refilled every run).
RunStats compute_run(double bulk_mean_ns, std::vector<uint64_t> &s)
{
    const double n{static_cast<double>(s.size())};

    double sum{0};
    for (uint64_t x : s)
    {
        sum += static_cast<double>(x);
    }
    double mean_ticks{sum / n};

    double sq{0};
    for (uint64_t x : s)
    {
        double d{static_cast<double>(x) - mean_ticks};
        sq += d * d;
    }
    double variance{(sq / n) * g_ns_per_tick * g_ns_per_tick};

    // Move the WORST_K largest samples to the end.
    auto worst_begin{s.end() - WORST_K};
    std::nth_element(s.begin(), worst_begin, s.end());
    double worst_sum{0};
    uint64_t worst_max{0};
    for (auto it{worst_begin}; it != s.end(); ++it)
    {
        worst_sum += static_cast<double>(*it);
        worst_max = std::max(worst_max, *it);
    }

    // Everything before worst_begin is <= the worst K, so the 99.9th
    // percentile can be found in that front part.
    auto p999{s.begin() + static_cast<std::ptrdiff_t>((n - 1) * 0.999)};
    std::nth_element(s.begin(), p999, worst_begin);

    return {
        bulk_mean_ns,
        variance,
        static_cast<double>(*p999) * g_ns_per_tick,
        worst_sum / WORST_K * g_ns_per_tick,
        static_cast<double>(worst_max) * g_ns_per_tick,
    };
}

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
    int at(size_t i) const { return v.at(i); }  // bounds-checked, like O1vector
    void set(size_t i, int x) { v.at(i) = x; }   // bounds-checked, like O1vector
};

struct MyVec
{
    O1vector<int> v{};
    void push(int x) { v.push(x); }
    int pop() { return v.pop(); }
    int at(size_t i) const { return v.at(i); }
    void set(size_t i, int x) { v.set(i, x); }
};

// =====================================================================
// One run of all four operations
// =====================================================================

struct AllRuns
{
    std::vector<RunStats> push{}, at{}, set{}, pop{};
};

template <typename Vec>
void one_run(const std::vector<size_t> &idx, std::vector<uint64_t> &samples, AllRuns *out)
{
    // ---- bulk pass: mean, no timer inside the loop ----
    double push_mean, at_mean, set_mean, pop_mean;
    {
        Vec vec;
        push_mean = time_bulk([&](size_t i)
                              { vec.push(static_cast<int>(i)); });
        at_mean = time_bulk([&](size_t i)
                            { g_sink = vec.at(idx[i]); });
        set_mean = time_bulk([&](size_t i)
                             { vec.set(idx[i], static_cast<int>(i)); });
        pop_mean = time_bulk([&](size_t)
                             { g_sink = vec.pop(); });
    }

    // ---- per-op pass: every op timed alone ----
    Vec vec;
    time_each(samples, [&](size_t i)
              { vec.push(static_cast<int>(i)); });
    RunStats push{compute_run(push_mean, samples)};

    time_each(samples, [&](size_t i)
              { g_sink = vec.at(idx[i]); });
    RunStats at{compute_run(at_mean, samples)};

    time_each(samples, [&](size_t i)
              { vec.set(idx[i], static_cast<int>(i)); });
    RunStats set{compute_run(set_mean, samples)};

    time_each(samples, [&](size_t)
              { g_sink = vec.pop(); });
    RunStats pop{compute_run(pop_mean, samples)};

    if (out != nullptr) // nullptr = warm-up run, results thrown away
    {
        out->push.push_back(push);
        out->at.push_back(at);
        out->set.push_back(set);
        out->pop.push_back(pop);
    }
}

void fill_random_indices(std::vector<size_t> &idx, unsigned seed)
{
    std::mt19937 rng{seed};
    std::uniform_int_distribution<size_t> dist{0, N - 1};
    for (auto &i : idx)
    {
        i = dist(rng);
    }
}

// =====================================================================
// Summary across runs + printing
// =====================================================================

struct Summary
{
    double mean_ns;
    double mean_ci95; // +- range on mean_ns
    double variance;
    double stddev;    // sqrt(variance)
    double p999_ns;
    double worst_ns;
    double max_ns;    // max over all runs
};

Summary summarise(const std::vector<RunStats> &runs)
{
    const double r{static_cast<double>(runs.size())};
    Summary s{};
    for (const RunStats &x : runs)
    {
        s.mean_ns += x.mean_ns / r;
        s.variance += x.variance / r;
        s.p999_ns += x.p999_ns / r;
        s.worst_ns += x.worst_ns / r;
        s.max_ns = std::max(s.max_ns, x.max_ns);
    }
    s.stddev = std::sqrt(s.variance);

    // 95% confidence range of the mean, from how much it moved run to run.
    double sq{0};
    for (const RunStats &x : runs)
    {
        double d{x.mean_ns - s.mean_ns};
        sq += d * d;
    }
    double run_sd{std::sqrt(sq / (r - 1))};
    s.mean_ci95 = 1.96 * run_sd / std::sqrt(r);
    return s;
}

std::string fmt_ns(double ns)
{
    if (ns >= 1e6)
        return std::format("{:.2f} ms", ns / 1e6);
    if (ns >= 1e3)
        return std::format("{:.2f} us", ns / 1e3);
    return std::format("{:.2f} ns", ns);
}

constexpr const char *ROW{"  {:<12} {:>16} {:>14} {:>11} {:>11} {:>13} {:>11}\n"};

void print_row(const std::string &name, const Summary &s)
{
    std::cout << std::format(ROW, name,
                             std::format("{:.3f} +-{:.3f}", s.mean_ns, s.mean_ci95),
                             std::format("{:.1f}", s.variance),
                             fmt_ns(s.stddev), fmt_ns(s.p999_ns),
                             fmt_ns(s.worst_ns), fmt_ns(s.max_ns));
}

std::string compare(const char *what, double std_v, double my_v)
{
    double ratio{my_v / std_v};
    if (ratio >= 1)
        return std::format("{} {:.2f}x worse", what, ratio);
    return std::format("{} {:.2f}x better", what, 1 / ratio);
}

void print_op(const std::string &op, const std::vector<RunStats> &stdr,
              const std::vector<RunStats> &myr)
{
    Summary a{summarise(stdr)};
    Summary b{summarise(myr)};

    std::cout << '\n'
              << op << '\n';
    std::cout << std::format(ROW, "", "mean ns", "variance ns^2", "stddev",
                             "p99.9", "worst-5 avg", "max");
    print_row("std::vector", a);
    print_row("O1vector", b);

    bool means_overlap{std::abs(a.mean_ns - b.mean_ns) <= a.mean_ci95 + b.mean_ci95};
    std::cout << "  O1vector: " << compare("mean", a.mean_ns, b.mean_ns)
              << (means_overlap ? " (ranges overlap: may be noise)" : "")
              << ", " << compare("worst-5 avg", a.worst_ns, b.worst_ns) << '\n';
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

    std::cout << std::format("{} runs x {} ops per operation per vector, T = int ({} bytes)\n",
                             RUNS, N, sizeof(int));
    std::cout << std::format("timer overhead subtracted: {} ticks ({:.1f} ns)\n",
                             g_overhead, static_cast<double>(g_overhead) * g_ns_per_tick);

    std::vector<size_t> idx(N);
    std::vector<uint64_t> samples(N); // reused by every run

    fill_random_indices(idx, 0);
    for (int w{0}; w < WARMUP_RUNS; ++w)
    {
        one_run<StdVec>(idx, samples, nullptr);
        one_run<MyVec>(idx, samples, nullptr);
    }

    AllRuns stdr, myr;
    for (int run{0}; run < RUNS; ++run)
    {
        std::cout << std::format("\rrun {}/{}", run + 1, RUNS) << std::flush;
        fill_random_indices(idx, static_cast<unsigned>(run + 1));

        // Take turns going first, so slow drift (heat, turbo) hits both equally.
        if (run % 2 == 0)
        {
            one_run<StdVec>(idx, samples, &stdr);
            one_run<MyVec>(idx, samples, &myr);
        }
        else
        {
            one_run<MyVec>(idx, samples, &myr);
            one_run<StdVec>(idx, samples, &stdr);
        }
    }
    std::cout << "\r          \r";

    print_op("push (empty -> N)", stdr.push, myr.push);
    print_op("at (random index)", stdr.at, myr.at);
    print_op("set (random index)", stdr.set, myr.set);
    print_op("pop (N -> empty)", stdr.pop, myr.pop);

    std::cout << "\nNotes:\n"
                 "  mean          = whole loop timed / N, averaged over runs; +- is a 95% range\n"
                 "  variance etc. = each op timed alone; stddev = sqrt(variance)\n"
                 "  worst-5 avg   = mean of the 5 slowest ops per run, averaged over runs\n"
                 "  max           = slowest single op across all runs (mostly OS noise)\n";
    return 0;
}
