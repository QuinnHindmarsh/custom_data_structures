#include "random.h"
#include "timer.h"
#include <iomanip>
#include <iostream>

// Random bytes aren't printable, so show them as hex.
// void print_hex(const std::string &s)
// {
//     std::cout << std::hex << std::setfill('0');
//     for (unsigned char c : s)
//         std::cout << std::setw(2) << static_cast<int>(c) << ' ';
//     std::cout << std::dec << std::setfill(' ') << '\n';
// }

int main()
{
    Timer t{};

    for (int i{0}; i < 2; ++i)
    {
        std::cout << "--- run " << i + 1 << " ---\n";
        std::cout << "get_range(1, 6):            " << random::get_range(1, 6) << '\n';
        std::cout << "get_range(-100, 100):       " << random::get_range(-100, 100) << '\n';
        std::cout << "get_range(1, (uint64_t)1e18): " << random::get_range(1, static_cast<uint64_t>(1e18)) << '\n';
        std::cout << "u32: " << random::u32() << '\n';
        std::cout << "i32: " << random::i32() << '\n';
        std::cout << "u64: " << random::u64() << '\n';
        std::cout << "i64: " << random::i64() << '\n';
        std::cout << "string(10): ";
        std::cout << random::string(10) << '\n';
    }

    std::cout << "\nTime elapsed: " << t.elapsed() << " s\n";
}
