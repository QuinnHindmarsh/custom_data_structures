#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <functional>
#include <bit>
#include <utility>

template <typename K, typename V>
struct slot
{
    K key{};
    V value{};
    size_t DIB{0}; // distance to initial bucket - 0 means bucket is empty
};

/*
OPTIMISATIONS/imrpovements
TODO: reduce the size of DIB? It doesnt really need to be 64 bits but technically chains could be that long, in practice it wouldnt relaly ever happen. but would be undefined if it did
TODO: I could do a struct slots that holds a vector of key,dib pairs, and a vector of values. This allows more elements to be loaded in the cache line as DIB and key need to be checked much more then value
TODO: for non athermaatic types use the current slot, for arithmatic ones overload and store the hash
TODO: I never resize downwards
TODO: ensure keys are hashable
TODO: take large keys as a reffernce in insert then make a copy if a insertion is needed and dont if only a update is needed

*/

template <typename K, typename V, typename hash = std::hash<K>, typename eq = std::equal_to<K>, const int resize_factor = 2, const float max_load_factor = 0.75f>
class open_map
{
public:
    // Undefined behaviour if capacity is not a power of 2 or less then 2
    explicit open_map(size_t capacity = 2) : m_capacity{capacity}, m_grow_at{static_cast<size_t>(capacity * max_load_factor)}, m_slots(capacity), m_shift{static_cast<uint8_t>(64 - std::countr_zero(capacity))}
    {
        assert(capacity >= 2 && (capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
    };
    void insert(K key, V value)
    {
        auto [existing_idx, probe_distance] = insertion_idx(key);
        if (m_slots[existing_idx].DIB != 0 && m_eq(m_slots[existing_idx].key, key))
        {
            m_slots[existing_idx].value = std::move(value);
            return;
        }

        if (m_size + 1 > m_grow_at)
        {
            // rehashing makes the existing index and probe distance no longer valid
            rehash(m_capacity * resize_factor);
            insert_no_check(std::move(key), std::move(value));
            ++m_size;
            return;
        }

        insert_no_check(std::move(key), std::move(value), existing_idx, probe_distance);
        ++m_size;
    }

    V *find(const K &key) // return nullptr if not found
    {
        return const_cast<V *>(std::as_const(*this).find(key));
    }

    const V *find(const K &key) const
    {
        auto [idx, _probe_cnt] = insertion_idx(key);
        if (m_slots[idx].DIB > 0 && m_eq(m_slots[idx].key, key))
        {
            return &m_slots[idx].value;
        }
        return nullptr;
    }

    void erase(const K &key)
    {
        size_t idx{index(key)};
        size_t probe_distance{1};

        while (true) // backwards shift deletion, so we can stop searching when we hit an empty slot
        {
            if (m_slots[idx].DIB == 0)
                return;
            if (m_eq(m_slots[idx].key, key))
            {
                break;
            }
            if (m_slots[idx].DIB < probe_distance)
                return;

            ++probe_distance;
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
        }
        size_t last_idx{idx};
        idx = (idx + 1) & (m_capacity - 1);

        while (m_slots[idx].DIB > 1) // backwards shift deletion
        {

            m_slots[last_idx] = std::move(m_slots[idx]);
            m_slots[last_idx].DIB--;
            last_idx = idx;
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
        }
        m_slots[last_idx].DIB = 0;
        --m_size;
    }

private:
    static_assert(resize_factor > 1 && (resize_factor & (resize_factor - 1)) == 0 && "resize factor must be > 1, and a power of 2 as hash & (cap -1) is used over %");
    static_assert(max_load_factor > 0.0f && max_load_factor < 1.0f && "max load factor must be between 0 and 1");
    size_t m_capacity{};
    size_t m_size{0};
    size_t m_grow_at;
    static constexpr std::size_t npos{static_cast<std::size_t>(-1)}; // max_size_t - used as a sentinal value
    std::vector<slot<K, V>> m_slots{};
    uint8_t m_shift;

    eq m_eq{};
    hash m_hash{};

    size_t index(const K &key) const
    {
        std::uint64_t hash_value = m_hash(key);
        // Fibonachi hashing
        return static_cast<size_t>((hash_value * 0x9E3779B97F4A7C15ull) >> m_shift); // returns top log2(m_capacity) bits of the hash value, which is equivalent to hash_value % m_capacity when m_capacity is a power of 2
    }

    void insert_no_check(K &&key, V &&value, size_t idx, size_t probe_length)
    {
        slot<K, V> cur{std::move(key), std::move(value), probe_length};

        while (m_slots[idx].DIB > 0)
        {
            if (cur.DIB > m_slots[idx].DIB)
            {
                std::swap(cur, m_slots[idx]);
            }
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2

            ++cur.DIB;
        }
        m_slots[idx] = std::move(cur);
    };

    // allows us to avoid comparasions when rehashing
    void insert_no_check(K &&key, V &&value)
    {
        size_t idx = index(key);
        slot<K, V> cur{std::move(key), std::move(value), 1};

        while (m_slots[idx].DIB > 0)
        {
            if (cur.DIB > m_slots[idx].DIB)
            {
                std::swap(cur, m_slots[idx]);
            }
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2

            ++cur.DIB;
        }
        m_slots[idx] = std::move(cur);
    }

    void rehash(size_t new_capacity)
    {
        assert((new_capacity & (new_capacity - 1)) == 0 && "new capacity must be a power of 2");
        std::vector<slot<K, V>> old_slots = std::move(m_slots);
        m_slots = std::vector<slot<K, V>>(new_capacity);
        m_capacity = new_capacity;
        m_grow_at = static_cast<size_t>(m_capacity * max_load_factor);
        m_shift = 64 - std::countr_zero(m_capacity);

        for (auto &s : old_slots)
        {
            if (s.DIB)
            {
                insert_no_check(std::move(s.key), std::move(s.value));
            }
        }
    }

    std::pair<size_t, size_t> insertion_idx(const K &key) const
    {
        size_t idx{index(key)};
        size_t probe_distance{1};

        while (true) // backwards shift deletion, so we can stop searching when we hit an empty slot
        {
            if (m_slots[idx].DIB < probe_distance) // if the DIB is 1, then we have reached the end of the cluster and the key is not in the map
            {
                return {idx, probe_distance};
            }
            if (m_eq(m_slots[idx].key, key))
            {
                return {idx, probe_distance};
            }

            ++probe_distance;
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
        }
        return {idx, probe_distance};
    }
};
