// Benchmark: open_map vs std::unordered_map.
//
// open_map is run once per max load factor (0.70, 0.75, 0.80, 0.85, 0.90 ), so each table shows which one is fastest.
//
// For each key type (uint32_t, uint64_t, std::string) at N = 1,000,000, every map runs the same phases:
//   insert      N new keys into an empty map (includes all rehashes)
//   update      the same N keys again (key already present, value overwritten)
//   find hit    N lookups of present keys, shuffled order
//   find miss   N lookups of keys that are not in the map
//   erase miss  N erases of keys that are not in the map (map is unchanged)
//   erase hit   erase all N keys, shuffled order
//   mixed       2N random ops over present + absent keys: 50% find, 25% insert, 25% erase
//
// Every measurement does at least ~1M ops. For small N, a pool of up to 64 maps is filled side by side
// and the cycle repeated `passes` times. At N = 1M that is just 1 map x 1 pass.
//
// Each measurement is repeated a few times and the BEST time is reported (least noise).
// A checksum of every value found is printed per map. If they differ, a map is returning wrong results.
//
// Results are written as Markdown tables (summary first, then one table per key type and N).
// Progress is printed to the console.
//
// Build with optimisation, -O0 numbers are meaningless (or use the "release (benchmark)" VS Code task):
//   g++ -std=c++23 -O2 -DNDEBUG open_map_benchmark.cpp -o open_map_benchmark
// Run:
//   ./open_map_benchmark                 writes open_map_benchmark_results.md
//   ./open_map_benchmark <file.md>       writes to <file.md>

#include "../../timer/timer.h"
#include "open_map.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using value_t = std::uint64_t;

// ---------- one interface over both maps ----------

// open_map with a given max load factor (resize factor 2, default hash/eq)
template <typename K, float LF>
using omap = open_map<K, value_t, std::hash<K>, std::equal_to<K>, 2, LF>;

constexpr std::array<float, 5> load_factors{0.7f, 0.75f, 0.80f, 0.85f, 0.90f};
constexpr std::size_t NUM_LF = load_factors.size();
constexpr std::size_t NUM_MAPS = NUM_LF + 1; // every load factor + std::unordered_map (last)

template <typename K, typename H, typename E, int R, float LF>
void map_insert(open_map<K, value_t, H, E, R, LF> &m, const K &k, value_t v) { m.insert(k, v); }
template <typename K>
void map_insert(std::unordered_map<K, value_t> &m, const K &k, value_t v) { m.insert_or_assign(k, v); }

template <typename K, typename H, typename E, int R, float LF>
const value_t *map_find(const open_map<K, value_t, H, E, R, LF> &m, const K &k) { return m.find(k); }
template <typename K>
const value_t *map_find(const std::unordered_map<K, value_t> &m, const K &k)
{
    auto it = m.find(k);
    return it == m.end() ? nullptr : &it->second;
}

template <typename K, typename H, typename E, int R, float LF>
void map_erase(open_map<K, value_t, H, E, R, LF> &m, const K &k) { m.erase(k); }
template <typename K>
void map_erase(std::unordered_map<K, value_t> &m, const K &k) { m.erase(k); }

// ---------- workload: the same keys and ops are fed to both maps ----------

template <typename K>
K make_key(std::uint64_t x);
template <>
std::uint32_t make_key<std::uint32_t>(std::uint64_t x) { return static_cast<std::uint32_t>(x); }
template <>
std::uint64_t make_key<std::uint64_t>(std::uint64_t x) { return x; }
template <>
std::string make_key<std::string>(std::uint64_t x) { return "key_" + std::to_string(x); } // ~24 chars, too long for SSO

enum Phase
{
    INSERT,
    UPDATE,
    FIND_HIT,
    FIND_MISS,
    ERASE_MISS,
    ERASE_HIT,
    MIXED,
    NUM_PHASES
};
constexpr const char *phase_names[NUM_PHASES]{"insert", "update", "find hit", "find miss", "erase miss", "erase hit", "mixed"};

struct Op
{
    std::uint8_t kind; // 0,1 = find   2 = insert   3 = erase
    std::uint32_t key_idx;
};

template <typename K>
struct Workload
{
    std::vector<K> present;          // inserted, in insertion order
    std::vector<K> present_shuffled; // same keys, different order
    std::vector<K> absent;           // never inserted
    std::vector<K> all;              // present + absent, used by mixed ops
    std::vector<Op> mixed;
};

// Needs 2N distinct keys.
template <typename K>
Workload<K> make_workload(std::size_t n, std::uint64_t seed)
{
    std::mt19937_64 rng{seed};

    std::unordered_set<K> seen;
    std::vector<K> keys;
    while (keys.size() < 2 * n)
    {
        K k = make_key<K>(rng());
        if (seen.insert(k).second)
            keys.push_back(k);
    }

    Workload<K> w;
    w.present.assign(keys.begin(), keys.begin() + n);
    w.absent.assign(keys.begin() + n, keys.end());

    w.present_shuffled = w.present;
    std::shuffle(w.present_shuffled.begin(), w.present_shuffled.end(), rng);

    w.all = keys;

    std::uniform_int_distribution<int> kind_dist(0, 3);
    std::uniform_int_distribution<std::uint32_t> idx_dist(0, static_cast<std::uint32_t>(w.all.size() - 1));
    for (std::size_t i = 0; i < 2 * n; ++i)
        w.mixed.push_back({static_cast<std::uint8_t>(kind_dist(rng)), idx_dist(rng)});

    return w;
}

// ---------- running ----------

constexpr std::size_t target_ops = 1'000'000;    // minimum ops per measurement
constexpr std::size_t max_pool_entries = 16'384; // pool of small maps stays cache sized

struct Plan
{
    std::size_t pool;   // maps filled side by side
    std::size_t passes; // full cycles per measurement
};

Plan make_plan(std::size_t n)
{
    const std::size_t pool = std::clamp<std::size_t>(max_pool_entries / n, 1, 64);
    const std::size_t passes = std::max<std::size_t>(1, target_ops / (n * pool));
    return {pool, passes};
}

using Times = std::array<double, NUM_PHASES>; // seconds per phase

// Times one phase: runs fn on every map in the pool, adds the elapsed time to total.
template <typename Map, typename Fn>
void timed(double &total, std::vector<Map> &pool, Fn fn)
{
    Timer t;
    for (Map &m : pool)
        fn(m);
    total += t.elapsed();
}

template <typename Map, typename K>
Times run_once(const Workload<K> &w, Plan plan, std::uint64_t &checksum)
{
    Times total{};
    const std::size_t n = w.present.size();

    for (std::size_t pass = 0; pass < plan.passes; ++pass)
    {
        std::vector<Map> pool(plan.pool);

        timed(total[INSERT], pool, [&](Map &m)
              { for (std::size_t i = 0; i < n; ++i) map_insert(m, w.present[i], i); });

        timed(total[UPDATE], pool, [&](Map &m)
              { for (std::size_t i = 0; i < n; ++i) map_insert(m, w.present[i], i + 1); });

        timed(total[FIND_HIT], pool, [&](Map &m)
              { for (const K &k : w.present_shuffled)
                    if (const value_t *p = map_find(m, k)) checksum += *p; });

        timed(total[FIND_MISS], pool, [&](Map &m)
              { for (const K &k : w.absent)
                    if (const value_t *p = map_find(m, k)) checksum += *p; }); // never hits if the map is correct

        timed(total[ERASE_MISS], pool, [&](Map &m)
              { for (const K &k : w.absent) map_erase(m, k); });

        timed(total[ERASE_HIT], pool, [&](Map &m)
              { for (const K &k : w.present_shuffled) map_erase(m, k); });
    }

    for (std::size_t pass = 0; pass < plan.passes; ++pass)
    {
        std::vector<Map> pool(plan.pool);
        for (Map &m : pool) // untimed setup
            for (std::size_t i = 0; i < n; ++i)
                map_insert(m, w.present[i], i);

        timed(total[MIXED], pool, [&](Map &m)
              {
                  for (const Op &op : w.mixed)
                  {
                      const K &k = w.all[op.key_idx];
                      if (op.kind < 2)
                      {
                          if (const value_t *p = map_find(m, k)) checksum += *p;
                      }
                      else if (op.kind == 2)
                          map_insert(m, k, op.key_idx);
                      else
                          map_erase(m, k);
                  } });
    }
    return total;
}

// Runs one map type once and keeps the best time per phase.
template <typename Map, typename K>
void run_into(const Workload<K> &w, Plan plan, Times &best, std::uint64_t &checksum)
{
    const Times t = run_once<Map>(w, plan, checksum);
    for (int p = 0; p < NUM_PHASES; ++p)
        best[p] = std::min(best[p], t[p]);
}

// Runs open_map once for every load factor in load_factors.
template <typename K, std::size_t... I>
void run_all_load_factors(const Workload<K> &w, Plan plan, std::array<Times, NUM_MAPS> &best,
                          std::array<std::uint64_t, NUM_MAPS> &sums, std::index_sequence<I...>)
{
    (run_into<omap<K, load_factors[I]>>(w, plan, best[I], sums[I]), ...);
}

// ---------- markdown output ----------

std::string with_commas(std::size_t n) // 1000000 -> "1,000,000"
{
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

struct Result
{
    std::string key_name;
    std::size_t n;
    std::array<double, NUM_PHASES> speedup; // unordered_map time / fastest open_map time
    std::string table;                      // markdown section with every time
    bool checksum_ok;
};

template <typename K>
Result bench(const char *key_name, std::size_t n)
{
    std::printf("%-12s N = %-10s ", key_name, with_commas(n).c_str());
    std::fflush(stdout);

    const Workload<K> w = make_workload<K>(n, 12345 + n);
    const Plan plan = make_plan(n);
    const std::size_t reps = n >= 1'000'000 ? 3 : 5;
    constexpr std::size_t STD = NUM_MAPS - 1; // index of std::unordered_map

    std::array<Times, NUM_MAPS> best;
    for (Times &t : best)
        t.fill(std::numeric_limits<double>::infinity());
    std::array<std::uint64_t, NUM_MAPS> sums{};

    for (std::size_t r = 0; r < reps; ++r) // interleave so all maps see the same machine state
    {
        run_all_load_factors(w, plan, best, sums, std::make_index_sequence<NUM_LF>{});
        run_into<std::unordered_map<K, value_t>>(w, plan, best[STD], sums[STD]);
    }

    Result res{key_name, n, {}, {}, true};
    for (std::size_t i = 0; i < NUM_LF; ++i)
        res.checksum_ok = res.checksum_ok && sums[i] == sums[STD];

    std::string &md = res.table;
    md += std::format("### {} keys, N = {}\n\n", key_name, with_commas(n));
    md += std::format("{} map(s) x {} pass(es), best of {} runs. Checksum: {}\n\n",
                      plan.pool, plan.passes, reps, res.checksum_ok ? "OK" : "**MISMATCH**");

    md += "| phase |";
    for (float lf : load_factors)
        md += std::format(" lf {:.2f} |", lf);
    md += " unordered_map | speedup |\n|:--|";
    for (std::size_t i = 0; i < NUM_MAPS + 1; ++i)
        md += "--:|";
    md += "\n";

    const double ops_per_phase = static_cast<double>(n * plan.pool * plan.passes);
    for (int p = 0; p < NUM_PHASES; ++p)
    {
        const double ops = p == MIXED ? 2 * ops_per_phase : ops_per_phase;
        std::size_t best_lf = 0;
        for (std::size_t i = 1; i < NUM_LF; ++i)
            if (best[i][p] < best[best_lf][p])
                best_lf = i;

        md += std::format("| {} |", phase_names[p]);
        for (std::size_t i = 0; i < NUM_LF; ++i)
        {
            const double ns = best[i][p] * 1e9 / ops;
            md += i == best_lf ? std::format(" **{:.1f}** |", ns) : std::format(" {:.1f} |", ns);
        }
        res.speedup[p] = best[STD][p] / best[best_lf][p];
        md += std::format(" {:.1f} | {:.2f}x |\n", best[STD][p] * 1e9 / ops, res.speedup[p]);
    }
    md += "\n";

    std::printf("done%s\n", res.checksum_ok ? "" : "  CHECKSUM MISMATCH");
    return res;
}

std::string build_markdown(const std::vector<Result> &results)
{
    std::string md = "# open_map benchmark results\n\n";

    char date[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof date, "%Y-%m-%d %H:%M", std::localtime(&now));
    md += std::format("- Run: {}\n", date);
#ifdef __VERSION__
    md += std::format("- Compiler: {}\n", __VERSION__);
#endif
#ifdef __OPTIMIZE__
    md += "- Optimised build: yes\n";
#else
    md += "- Optimised build: **NO, these numbers are meaningless. Build with -O2.**\n";
#endif
#ifdef NDEBUG
    md += "- Asserts: off\n\n";
#else
    md += "- Asserts: on (build with -DNDEBUG)\n\n";
#endif

    md += "## Summary: fastest open_map vs std::unordered_map\n\n";
    md += "Speedup = unordered_map time / time of the fastest open_map load factor. "
          "Above 1x means open_map is faster. **Bold** = open_map is slower.\n\n";
    md += "| keys | N |";
    for (const char *name : phase_names)
        md += std::format(" {} |", name);
    md += "\n|:--|--:|";
    for (int p = 0; p < NUM_PHASES; ++p)
        md += "--:|";
    md += "\n";
    for (const Result &r : results)
    {
        md += std::format("| {} | {} |", r.key_name, with_commas(r.n));
        for (double s : r.speedup)
            md += s < 1.0 ? std::format(" **{:.2f}x** |", s) : std::format(" {:.2f}x |", s);
        md += "\n";
    }

    md += "\n## Details\n\n";
    md += "Time per operation in ns, lower is better. **Bold** = fastest open_map load factor. "
          "Speedup = unordered_map / fastest open_map.\n\n";
    md += "Phases: **insert** N new keys into an empty map. **update** the same keys again. "
          "**find hit / miss** look up present / absent keys. **erase miss** erase absent keys. "
          "**erase hit** erase every key. **mixed** 50% find, 25% insert, 25% erase.\n\n";
    for (const Result &r : results)
        md += r.table;
    return md;
}

int main(int argc, char **argv)
{
    const std::string out_path = argc > 1 ? argv[1] : "custom_data_structures\\hashmap\\open_map\\open_map_benchmark_results.md";
    std::vector<Result> results;

    constexpr std::size_t n = 1'000'000;
    results.push_back(bench<std::uint32_t>("uint32_t", n));
    results.push_back(bench<std::uint64_t>("uint64_t", n));
    results.push_back(bench<std::string>("std::string", n));

    std::ofstream out{out_path};
    if (!out)
    {
        std::fprintf(stderr, "could not open %s for writing\n", out_path.c_str());
        return 1;
    }
    out << build_markdown(results);
    std::printf("\nwrote %s\n", out_path.c_str());
    return 0;
}
