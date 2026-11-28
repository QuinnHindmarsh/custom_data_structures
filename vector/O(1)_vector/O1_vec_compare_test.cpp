// Correctness tests: O1vector vs std::vector
//
// The idea:
//   Do the exact same operations to an O1vector and a std::vector.
//   std::vector is the "answer key". After every operation, both must
//   hold the same elements in the same order.
//
// Build & run:
//   g++ -std=c++20 -Wall -Wextra -g O1_vec_compare_test.cpp -o O1_vec_compare_test.exe
//   ./O1_vec_compare_test.exe

#include <algorithm>
#include <format>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "O(1)_vector.h"

// The iterator must satisfy the C++20 concepts, or std::ranges algorithms won't accept it.
// These are compile time checks: if one fails the file doesn't build.
static_assert(std::contiguous_iterator<O1vector<int>::const_iterator>);
static_assert(std::ranges::contiguous_range<O1vector<int>>);
static_assert(std::ranges::sized_range<O1vector<int>>);
static_assert(std::ranges::contiguous_range<const O1vector<std::string>>);
static_assert(std::is_same_v<std::iter_reference_t<O1vector<int>::iterator>, const int &>); // read-only

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

            if (m_mine[i] != m_expected[i])
            {
                report(std::format("[{}] is {}, std has {}", i, m_mine[i], m_expected[i]));
                return false;
            }

            // [] must return a reference to the stored element, not a copy
            if (&m_mine[i] != m_mine.at_ptr(i))
            {
                report(std::format("&[{}] and at_ptr({}) point at different elements", i, i));
                return false;
            }
        }

        // iterators must walk exactly the same elements, forwards and backwards.
        // Runs after every operation, so this also covers mid-grow / mid-shrink.
        if (!std::equal(m_mine.begin(), m_mine.end(), m_expected.begin(), m_expected.end()))
        {
            report("begin()..end() doesn't match std");
            return false;
        }
        if (!std::equal(m_mine.rbegin(), m_mine.rend(), m_expected.rbegin(), m_expected.rend()))
        {
            report("rbegin()..rend() doesn't match std");
            return false;
        }
        if (static_cast<size_t>(m_mine.end() - m_mine.begin()) != m_mine.size())
        {
            report("end() - begin() != size()");
            return false;
        }
        return true;
    }

    size_t size() const { return m_expected.size(); }
    const O1vector<T> &mine() const { return m_mine; }

private:
    O1vector<T> m_mine;
    std::vector<T> m_expected{};

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

// operator[] on its own: reads the right element after every push (so
// also mid-grow and mid-shrink), sees writes made by set(), and works on a
// const vector.
bool test_index_operator()
{
    O1vector<int> mine;
    for (int i{0}; i < 1000; ++i)
    {
        mine.push(i * 3);
        CHECK(mine[0] == 0);
        CHECK(mine[mine.size() - 1] == i * 3);
        CHECK(mine[mine.size() / 2] == mine.at(mine.size() / 2));
    }

    mine.set(0, -1);   // already copied if a resize is running
    mine.set(999, -2); // last element
    CHECK(mine[0] == -1);
    CHECK(mine[999] == -2);

    const O1vector<int> &as_const = mine;
    CHECK(as_const[0] == -1);
    CHECK(&as_const[500] == mine.at_ptr(500));

    // pop down through shrinks, [] still lines up with at()
    while (mine.size() > 1)
    {
        mine.pop_no_return();
        CHECK(mine[mine.size() - 1] == mine.at(mine.size() - 1));
        CHECK(mine[0] == -1);
    }
    return true;
}

// Returns e.what() of the std::out_of_range that `code` throws, or "" if it
// doesn't throw. Any other exception type escapes and fails the test.
template <typename Code>
std::string out_of_range_message(Code code)
{
    try
    {
        code();
    }
    catch (const std::out_of_range &e)
    {
        return e.what();
    }
    return "";
}

// Errors are std::out_of_range (like std::vector::at), and the message
// names the function, the bad index and the size.
bool test_error_messages()
{
    O1vector<int> mine;

    CHECK(out_of_range_message([&]
                               { mine.pop(); }) == "O1vector::pop(): can't pop, the vector is empty");
    CHECK(out_of_range_message([&]
                               { mine.pop_no_return(); }) == "O1vector::pop_no_return(): can't pop, the vector is empty");
    CHECK(out_of_range_message([&]
                               { mine.at(0); }) == "O1vector::at(0): index out of range, the vector is empty");

    for (int i{0}; i < 3; ++i)
    {
        mine.push(i);
    }

    CHECK(out_of_range_message([&]
                               { mine.at(3); }) == "O1vector::at(3): index out of range, size is 3 (valid indices are 0 to 2)");
    CHECK(out_of_range_message([&]
                               { mine.at_ptr(10); }) == "O1vector::at_ptr(10): index out of range, size is 3 (valid indices are 0 to 2)");
    CHECK(out_of_range_message([&]
                               { mine.set(7, 1); }) == "O1vector::set(7): index out of range, size is 3 (valid indices are 0 to 2)");

    // valid calls don't throw
    CHECK(out_of_range_message([&]
                               { mine.at(2); }) == "");
    return true;
}

// Range-for visits every element in order, also while a resize is running.
bool test_range_for()
{
    O1vector<int> mine;
    for (int i{0}; i < 1000; ++i)
    {
        mine.push(i);

        int expected{0};
        for (const int &x : mine)
        {
            CHECK(x == expected);
            CHECK(&x == mine.at_ptr(static_cast<size_t>(expected))); // a reference into storage, not a copy
            ++expected;
        }
        CHECK(expected == i + 1);
    }

    O1vector<int> empty;
    for ([[maybe_unused]] int x : empty)
    {
        CHECK(false); // an empty vector must not run the loop body
    }
    CHECK(empty.begin() == empty.end());
    CHECK(empty.rbegin() == empty.rend());
    return true;
}

// Every operator a random access iterator needs gives the same answer as raw index maths.
bool test_iterator_arithmetic()
{
    O1vector<int> mine;
    for (int i{0}; i < 50; ++i)
    {
        mine.push(i * 2);
    }

    auto it = mine.begin();
    CHECK(*it == 0);
    CHECK(*(it + 10) == 20);
    CHECK(*(10 + it) == 20);
    CHECK(it[7] == 14);

    auto copy = it++; // post-increment returns the old position
    CHECK(*copy == 0);
    CHECK(*it == 2);
    CHECK(*++it == 4);
    CHECK(*it-- == 4);
    CHECK(*it == 2);
    CHECK(*--it == 0);

    it += 49;
    CHECK(*it == 98);
    it -= 9;
    CHECK(*it == 80);
    CHECK(*(it - 40) == 0);
    CHECK(it - mine.begin() == 40);
    CHECK(mine.begin() - it == -40);
    CHECK(it + 10 == mine.end());

    CHECK(mine.begin() < it);
    CHECK(it > mine.begin());
    CHECK(mine.begin() <= mine.begin());
    CHECK(mine.end() >= it);
    CHECK(mine.begin() != it);
    CHECK((mine.begin() <=> mine.end()) < 0);

    CHECK(mine.cbegin() == mine.begin());
    CHECK(mine.cend() == mine.end());
    CHECK(*mine.rbegin() == 98);
    CHECK(*mine.crbegin() == 98);
    CHECK(*(mine.rend() - 1) == 0);

    O1vector<int>::const_iterator default_made{}; // default constructible, equal to other default ones
    CHECK(default_made == O1vector<int>::const_iterator{});

    O1vector<std::string> strings;
    strings.push("hello");
    CHECK(strings.begin()->size() == 5); // operator->
    return true;
}

// Standard algorithms and ranges work, and the iterators are truly contiguous.
bool test_std_algorithms()
{
    O1vector<int> mine;
    std::vector<int> expected;
    for (int i{0}; i < 777; ++i) // 777 so the vector is mid-resize when we start reading
    {
        mine.push(i % 37);
        expected.push_back(i % 37);
    }

    CHECK(std::accumulate(mine.begin(), mine.end(), 0L) == std::accumulate(expected.begin(), expected.end(), 0L));
    CHECK(std::count(mine.begin(), mine.end(), 5) == std::count(expected.begin(), expected.end(), 5));
    CHECK(std::find(mine.begin(), mine.end(), 36) - mine.begin() == std::find(expected.begin(), expected.end(), 36) - expected.begin());
    CHECK(*std::max_element(mine.begin(), mine.end()) == 36);
    CHECK(std::distance(mine.begin(), mine.end()) == 777);

    // C++20 ranges
    CHECK(std::ranges::equal(mine, expected));
    CHECK(std::ranges::find(mine, 10) - mine.begin() == 10);
    CHECK(std::ranges::distance(mine) == 777);
    CHECK(std::ranges::size(mine) == 777);

    auto evens = mine | std::views::filter([](int x) { return x % 2 == 0; });
    auto expected_evens = expected | std::views::filter([](int x) { return x % 2 == 0; });
    CHECK(std::ranges::equal(evens, expected_evens));
    CHECK(std::ranges::equal(mine | std::views::reverse, expected | std::views::reverse));

    // copy out into a std::vector, then sort that copy (O1vector itself is read-only)
    std::vector<int> copied(mine.begin(), mine.end());
    CHECK(copied == expected);
    std::ranges::sort(copied);
    CHECK(std::ranges::is_sorted(copied));

    // contiguous: std::span can view the storage directly, and to_address gives the real pointer
    std::span<const int> view{mine.begin(), mine.end()};
    CHECK(view.size() == mine.size());
    CHECK(view.data() == mine.at_ptr(0));
    CHECK(std::to_address(mine.begin() + 100) == mine.at_ptr(100));

    // std::binary_search on a sorted O1vector
    O1vector<int> sorted;
    for (int i{0}; i < 500; ++i)
    {
        sorted.push(i * 3);
    }
    CHECK(std::binary_search(sorted.begin(), sorted.end(), 297));
    CHECK(!std::binary_search(sorted.begin(), sorted.end(), 298));
    CHECK(*std::ranges::lower_bound(sorted, 298) == 300);
    return true;
}

// Iterators see values written by set() (set does not invalidate them).
bool test_iterators_see_set()
{
    O1vector<int> mine;
    for (int i{0}; i < 300; ++i)
    {
        mine.push(i);
    }
    auto it = mine.begin() + 5;
    mine.set(5, -5);
    CHECK(*it == -5);
    mine.set(0, -1);
    CHECK(*mine.begin() == -1);
    CHECK(*std::ranges::min_element(mine) == -5);
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
    run("operator[]", test_index_operator);
    run("error messages", test_error_messages);
    run("range for", test_range_for);
    run("iterator arithmetic", test_iterator_arithmetic);
    run("std algorithms", test_std_algorithms);
    run("iterators see set", test_iterators_see_set);

    std::cout << '\n'
              << (g_failed == 0 ? "ALL PASSED" : std::format("{} TEST(S) FAILED", g_failed)) << '\n';
    return g_failed == 0 ? 0 : 1;
}
