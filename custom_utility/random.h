#pragma once

#include <chrono>
#include <random>
#include <cstdint>
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

    // T is deduced from min only, so get_range((uint64_t)1, 5) works.
    // Requires min <= max.
    template <typename T>
    inline T get_range(std::type_identity_t<T> min, T max)
    {
        if constexpr (sizeof(T) <= 4)
            return std::uniform_int_distribution<T>(min, max)(mt);
        else
            return std::uniform_int_distribution<T>(min, max)(mt64);
    }

    inline uint32_t get_u32()
    {
        return static_cast<uint32_t>(mt());
    }

    inline int32_t get_i32()
    {
        return static_cast<int32_t>(mt());
    }

    inline uint64_t get_u64()
    {

        return static_cast<uint64_t>(mt64());
    }

    inline int64_t get_i64()
    {
        return static_cast<int64_t>(mt64());
    }

};
