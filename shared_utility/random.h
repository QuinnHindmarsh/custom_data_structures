#pragma once

#include <chrono>
#include <random>
#include <cstdint>
#include <string>
#include <type_traits>

namespace random
{
    inline std::seed_seq generate_seed()
    {
        std::random_device rd{};
        return std::seed_seq{static_cast<std::seed_seq::result_type>(std::chrono::steady_clock::now().time_since_epoch().count()),
                             rd(), rd(), rd(), rd(), rd(), rd(), rd()};
    }

    inline std::seed_seq ss{generate_seed()};
    inline std::mt19937 mt{ss}; // mersenne twister, 32b
    inline std::mt19937_64 mt64{ss};

    // T is deduced from max only, so get_range(1, (uint64_t)5) works.
    // Requires min <= max.
    template <typename T>
    inline T range(std::type_identity_t<T> min, T max)
    {
        if constexpr (sizeof(T) <= 4)
            return std::uniform_int_distribution<T>(min, max)(mt);
        else
            return std::uniform_int_distribution<T>(min, max)(mt64);
    }

    inline uint32_t u32()
    {
        return static_cast<uint32_t>(mt());
    }

    inline int32_t i32()
    {
        return static_cast<int32_t>(mt());
    }

    inline uint64_t u64()
    {

        return static_cast<uint64_t>(mt64());
    }

    inline int64_t i64()
    {
        return static_cast<int64_t>(mt64());
    }

    inline std::string string(size_t length)
    {
        std::string s(length, '\0');
        size_t i = 0;

        while (i < length)
        {
            uint64_t num = u64();
            for (int j{0}; (j < 8) && i < length; ++j)
            {
                s[i] = static_cast<char>((num >> (j * 8)) & 0xFF);
                ++i;
            }
        }
        return s;
    }

};
