// Differential test: open_map vs std::unordered_map.
// 1M random operations (insert / erase / find / update-through-find) are applied to both maps,
// and after every operation the result of find(key) is compared.
//
// Because find/erase take `const K &`, each op also checks:
//   - the const find() overload agrees with the non-const one (same pointer)
//   - find() works with a temporary key (a temporary binds to const K &)
//   - writing through the V* that find() returns really changes the stored value
//
// Build: g++ -std=c++23 -g -O1 open_map_differental_test.cpp -o test
// Run:   ./test          (or ./test <seed> to try a different random sequence)

#include "open_map.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

constexpr int num_ops = 1'000'000;

// ---------- random key/value generators, one per type ----------
// Keys come from a limited range so operations often hit keys that already exist.

template <typename T>
T random_value(std::mt19937_64 &rng, int range);

template <>
int random_value<int>(std::mt19937_64 &rng, int range)
{
    return std::uniform_int_distribution<int>(-range, range)(rng);
}

template <>
std::uint64_t random_value<std::uint64_t>(std::mt19937_64 &rng, int range)
{
    return std::uniform_int_distribution<std::uint64_t>(0, range)(rng) * 0x100000001ull; // spread out over 64 bits
}

template <>
char random_value<char>(std::mt19937_64 &rng, int)
{
    return static_cast<char>(std::uniform_int_distribution<int>(-128, 127)(rng));
}

template <>
double random_value<double>(std::mt19937_64 &rng, int range)
{
    return std::uniform_int_distribution<int>(0, range)(rng) * 0.5;
}

template <>
std::string random_value<std::string>(std::mt19937_64 &rng, int range)
{
    // mix of short strings (fit in std::string's small buffer) and long ones (heap allocated)
    int n = std::uniform_int_distribution<int>(0, range)(rng);
    return (n % 2 ? "k" : "a_much_longer_key_that_needs_the_heap_") + std::to_string(n);
}

// print chars as numbers so the failure message is readable
template <typename T>
auto printable(const T &v)
{
    if constexpr (std::is_same_v<T, char>)
        return static_cast<int>(v);
    else
        return v;
}

// ---------- the test ----------

template <typename K, typename V>
bool run_test(const char *name, std::uint64_t seed, int key_range)
{
    open_map<K, V> mine;
    std::unordered_map<K, V> ref;
    std::mt19937_64 rng(seed);

    int inserts = 0, erases = 0, finds = 0, updates = 0;

    for (int op = 0; op < num_ops; ++op)
    {
        K key = random_value<K>(rng, key_range);
        int kind = std::uniform_int_distribution<int>(0, 3)(rng);
        const char *op_name = "";

        if (kind == 0)
        {
            V value = random_value<V>(rng, 1'000'000);
            mine.insert(key, value);
            ref.insert_or_assign(key, value);
            op_name = "insert";
            ++inserts;
        }
        else if (kind == 1)
        {
            mine.erase(key);
            ref.erase(key);
            op_name = "erase";
            ++erases;
        }
        else if (kind == 2)
        {
            op_name = "find";
            ++finds;
        }
        else
        {
            // update through the pointer find() returns: *find(key) = new value
            op_name = "update via find";
            ++updates;
            V value = random_value<V>(rng, 1'000'000);
            if (V *p = mine.find(key))
                *p = value;
            if (auto it = ref.find(key); it != ref.end())
                it->second = value;
        }

        // compare find(key) after every operation
        V *got = mine.find(key);
        auto it = ref.find(key);
        bool ok = (got == nullptr) == (it == ref.end()) && (got == nullptr || *got == it->second);

        // const overload must point at the same value as the non-const one
        const V *got_const = std::as_const(mine).find(key);
        if (got_const != got)
        {
            std::cout << "FAIL " << name << " (seed " << seed << ") at op #" << op << ": " << op_name << "("
                      << printable(key) << ")\n  const find() and non-const find() returned different pointers\n";
            return false;
        }

        // a temporary key must find the same slot
        V *got_temp = mine.find(K{key});
        if (got_temp != got)
        {
            std::cout << "FAIL " << name << " (seed " << seed << ") at op #" << op << ": " << op_name << "("
                      << printable(key) << ")\n  find(temporary key) returned a different pointer\n";
            return false;
        }

        if (!ok)
        {
            std::cout << "FAIL " << name << " (seed " << seed << ") at op #" << op << ": " << op_name << "("
                      << printable(key) << ")\n  open_map:      ";
            if (got)
                std::cout << printable(*got) << '\n';
            else
                std::cout << "not found\n";
            std::cout << "  unordered_map: ";
            if (it != ref.end())
                std::cout << printable(it->second) << '\n';
            else
                std::cout << "not found\n";
            return false;
        }
    }

    // final check: every key unordered_map holds must be in open_map with the same value
    const auto &const_mine = mine; // check through the const overload
    for (const auto &[key, value] : ref)
    {
        const V *got = const_mine.find(key);
        if (!got || *got != value)
        {
            std::cout << "FAIL " << name << " (seed " << seed << ") final check: key " << printable(key)
                      << " missing or wrong in open_map\n";
            return false;
        }
    }

    // check size too, if open_map has a size() function
    if constexpr (requires { mine.size(); })
    {
        if (mine.size() != ref.size())
        {
            std::cout << "FAIL " << name << " (seed " << seed << ") final size: open_map " << mine.size()
                      << ", unordered_map " << ref.size() << '\n';
            return false;
        }
    }

    std::cout << "PASS " << name << " (" << inserts << " inserts, " << erases << " erases, " << finds
              << " finds, " << updates << " updates, final size " << ref.size() << ")\n";
    return true;
}

int main(int argc, char **argv)
{
    std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 12345;

    bool all_ok = true;
    all_ok &= run_test<int, int>("int -> int", seed, 10'000);
    all_ok &= run_test<std::string, std::string>("string -> string", seed, 10'000);
    all_ok &= run_test<std::uint64_t, double>("uint64 -> double", seed, 50'000);
    all_ok &= run_test<double, int>("double -> int", seed, 10'000);
    all_ok &= run_test<char, std::string>("char -> string", seed, 0); // only 256 keys: lots of updates/re-inserts

    std::cout << (all_ok ? "\nAll tests passed.\n" : "\nSome tests FAILED.\n");
    return all_ok ? 0 : 1;
}
