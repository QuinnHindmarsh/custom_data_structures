// Correctness tests: O1vector vs std::vector
//
// The idea:
//   Do the exact same operations to an O1vector and a std::vector.
//   std::vector is the "answer key". After every operation, both must
//   hold the same elements in the same order.
//
// Build & run:
//   g++ -std=c++20 -Wall -g O1_vec_compare_test.cpp -o O1_vec_compare_test.exe
//   ./O1_vec_compare_test.exe

#include <format>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "O(1)_vector.h"

// =====================================================================
// Helpers
// =====================================================================

// If the condition is false: print what failed, and fail the test.
#define CHECK(condition)                                                             \
    if (!(condition))                                                                \
    {                                                                                \
        std::cout << "    CHECK failed: " #condition " (line " << __LINE__ << ")\n"; \
        return false;                                                                \
    }

// Returns true if running `code` throws any exception.
template <typename Code>
bool throws(Code code)
{
    try
    {
        code();
    }
    catch (...)
    {
        return true;
    }
    return false;
}

// Holds an O1vector and a std::vector side by side.
// Every operation is applied to BOTH, so they should always match.
template <typename T>
class Both
{
public:
    explicit Both(size_t capacity = 1) : m_mine{capacity} {}

    void push(const T &value)
    {
        remember(std::format("push({})", value));
        m_mine.push(value);
        m_expected.push_back(value);
    }

    // Pops from both. Returns false if they gave back different values.
    bool pop()
    {
        remember("pop()");
        T mine_value = m_mine.pop();
        T expected_value = m_expected.back();
        m_expected.pop_back();

        if (mine_value != expected_value)
        {
            std::cout << "    after op #" << m_op_count << " " << m_last_op
                      << ": popped " << mine_value << ", std popped " << expected_value << '\n';
            return false;
        }
        return true;
    }

    void set(size_t idx, const T &value)
    {
        remember(std::format("set({}, {})", idx, value));
        m_mine.set(idx, value);
        m_expected[idx] = value;
    }

    // True if both vectors hold the same elements in the same order.
    // On a mismatch, prints the first difference and the last operation.
    bool same() const
    {
        if (m_mine.size() != m_expected.size())
        {
            report(std::format("size is {}, std size is {}", m_mine.size(), m_expected.size()));
            return false;
        }

        if (m_mine.not_empty() == m_expected.empty())
        {
            report("not_empty() is wrong");
            return false;
        }

        for (size_t i{0}; i < m_expected.size(); ++i)
        {
            if (m_mine.at(i) != m_expected[i])
            {
                report(std::format("element [{}] is {}, std has {}", i, m_mine.at(i), m_expected[i]));
                return false;
            }

            if (*m_mine.at_ptr(i) != m_expected[i])
            {
                report(std::format("*at_ptr({}) is {}, std has {}", i, *m_mine.at_ptr(i), m_expected[i]));
                return false;
            }
        }
        return true;
    }

    size_t size() const { return m_expected.size(); }

private:
    O1vector<T> m_mine;
    std::vector<T> m_expected;

    // Only used to make failure messages helpful.
    std::string m_last_op{"(nothing yet)"};
    int m_op_count{0};

    void remember(const std::string &op)
    {
        m_last_op = op;
        ++m_op_count;
    }

    void report(const std::string &problem) const
    {
        std::cout << "    after op #" << m_op_count << " " << m_last_op << ": " << problem << '\n';
    }
};

// =====================================================================
// Tests
// Each test returns true on pass, false on fail.
// =====================================================================

// A new vector should be empty.
bool test_starts_empty()
{
    Both<int> v;
    CHECK(v.same());
    return true;
}

// Push a handful of values.
bool test_push_a_few()
{
    Both<int> v;
    for (int i{0}; i < 5; ++i)
    {
        v.push(i);
        CHECK(v.same());
    }
    return true;
}

// Push lots of values. This forces many grows.
bool test_push_many()
{
    Both<int> v;
    for (int i{0}; i < 2000; ++i)
    {
        v.push(i);
        CHECK(v.same());
    }
    return true;
}

// Fill up, then pop everything. This forces many shrinks.
// Each pop must return the same value std::vector's back() had.
bool test_pop_everything()
{
    Both<int> v;
    for (int i{0}; i < 2000; ++i)
    {
        v.push(i);
    }

    while (v.size() > 0)
    {
        CHECK(v.pop());
        CHECK(v.same());
    }
    return true;
}

// Overwrite existing elements with set().
bool test_set()
{
    Both<int> v;
    for (int i{0}; i < 100; ++i)
    {
        v.push(i);
    }

    for (size_t i{0}; i < v.size(); i += 2)
    {
        v.set(i, -1);
        CHECK(v.same());
    }
    return true;
}

// set() right after every push, so some sets land while a resize is
// half-done. Index 0 is always already copied to the new array, so this
// checks that set() updates the new array too.
bool test_set_during_resize()
{
    Both<int> v;
    for (int i{0}; i < 1000; ++i)
    {
        v.push(i);
        v.set(0, -i);                // already copied (if a resize is running)
        v.set(v.size() - 1, i * 10); // not copied yet
        CHECK(v.same());
    }

    // Pop everything, setting index 0 as we go (shrinks in progress).
    while (v.size() > 0)
    {
        v.set(0, 7);
        CHECK(v.pop());
        CHECK(v.same());
    }
    return true;
}

// Go up and down repeatedly so pushes and pops happen in the middle of
// both grows and shrinks.
bool test_up_and_down()
{
    Both<int> v;
    int next{0};

    for (int round{0}; round < 20; ++round)
    {
        for (int i{0}; i < 300; ++i)
        {
            v.push(next++);
            CHECK(v.same());
        }
        for (int i{0}; i < 250; ++i)
        {
            CHECK(v.pop());
            CHECK(v.same());
        }
    }
    return true;
}

// Different starting capacities should all behave the same.
bool test_starting_capacities()
{
    for (size_t capacity : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{64}, size_t{1000}})
    {
        Both<int> v{capacity};
        for (int i{0}; i < 300; ++i)
        {
            v.push(i);
            CHECK(v.same());
        }
        while (v.size() > 0)
        {
            CHECK(v.pop());
            CHECK(v.same());
        }
    }
    return true;
}

// Thousands of random push / pop / set operations.
// Uses a fixed seed, so a failure happens the same way every run.
bool test_random_ints()
{
    std::mt19937 rng{42};
    std::uniform_int_distribution<int> percent{1, 100};
    std::uniform_int_distribution<int> value{-1000, 1000};

    Both<int> v;
    for (int op{0}; op < 20000; ++op)
    {
        int roll{percent(rng)};

        if (roll <= 50 || v.size() == 0) // 50%: push
        {
            v.push(value(rng));
        }
        else if (roll <= 80) // 30%: pop
        {
            CHECK(v.pop());
        }
        else // 20%: set a random existing index
        {
            std::uniform_int_distribution<size_t> index{0, v.size() - 1};
            v.set(index(rng), value(rng));
        }

        CHECK(v.same());
    }
    return true;
}

// Same random test, but with std::string.
// Strings own memory, so bad copies/moves show up as wrong or empty text.
bool test_random_strings()
{
    std::mt19937 rng{7};
    std::uniform_int_distribution<int> percent{1, 100};
    std::uniform_int_distribution<int> value{0, 1000};

    Both<std::string> v;
    for (int op{0}; op < 20000; ++op)
    {
        int roll{percent(rng)};
        std::string text{"str_" + std::to_string(value(rng))};

        if (roll <= 50 || v.size() == 0)
        {
            v.push(text);
        }
        else if (roll <= 80)
        {
            CHECK(v.pop());
        }
        else
        {
            std::uniform_int_distribution<size_t> index{0, v.size() - 1};
            v.set(index(rng), text);
        }

        CHECK(v.same());
    }
    return true;
}

// Bad indexes should throw, like std::vector::at() does.
bool test_out_of_bounds_throws()
{
    O1vector<int> mine;
    std::vector<int> expected;

    // Empty vector: everything should throw.
    CHECK(throws([&]
                 { mine.pop(); }));
    CHECK(throws([&]
                 { mine.at(0); }));
    CHECK(throws([&]
                 { mine.at_ptr(0); }));
    CHECK(throws([&]
                 { mine.set(0, 1); }));
    CHECK(throws([&]
                 { (void)expected.at(0); })); // std agrees

    for (int i{0}; i < 3; ++i)
    {
        mine.push(i);
        expected.push_back(i);
    }

    // Index 3 is one past the end -> throws.
    CHECK(throws([&]
                 { mine.at(3); }));
    CHECK(throws([&]
                 { mine.set(3, 1); }));
    CHECK(throws([&]
                 { (void)expected.at(3); })); // std agrees

    // Index 2 is the last element -> fine.
    CHECK(!throws([&]
                  { mine.at(2); }));
    CHECK(!throws([&]
                  { mine.set(2, 1); }));
    return true;
}

// =====================================================================
// Runner
// =====================================================================

int g_failed{0};

void run(const char *name, bool (*test)())
{
    std::cout << name << '\n';

    bool passed{false};
    try
    {
        passed = test();
    }
    catch (const std::exception &e)
    {
        std::cout << "    unexpected exception: " << e.what() << '\n';
    }

    std::cout << (passed ? "    PASS\n" : "    FAIL\n");
    if (!passed)
    {
        ++g_failed;
    }
}

int main()
{
    run("starts empty", test_starts_empty);
    run("push a few", test_push_a_few);
    run("push many", test_push_many);
    run("pop everything", test_pop_everything);
    run("set", test_set);
    run("set during resize", test_set_during_resize);
    run("up and down", test_up_and_down);
    run("starting capacities", test_starting_capacities);
    run("random ints", test_random_ints);
    run("random strings", test_random_strings);
    run("out of bounds throws", test_out_of_bounds_throws);

    std::cout << '\n'
              << (g_failed == 0 ? "ALL PASSED" : std::format("{} TEST(S) FAILED", g_failed)) << '\n';
    return g_failed == 0 ? 0 : 1;
}
