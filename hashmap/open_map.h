#pragma once
#include <vector>
#include <cstdint>

template <typename K, typename V>
struct slot
{
    K key{};
    V value{};
    size_t DIB{0}; // distance to initial bucket
    bool occupied{false};
};

template <typename K, typename V, typename hash = std::hash<K>, typename eq = std::equal_to<K>, int resize_factor = 2>
class open_map
{
public:
    explicit open_map(int size = 0) : m_size{size} {}

    void insert(int key, int value)
    {
        if (m_size + 1 > m_capacity * m_max_load_factor)
        {
            rehash(m_capacity * resize_factor);
        }

        size_t idx{index(key)};
        size_t dib{0};
        while (m_slots[idx].occupied)
        {
            if (m_eq(m_slots[idx].key, key)) // updating existing key
            {
                m_slots[idx].value = value;
                return;
            }
            // TODO: test with other probing (quadratic, double hashing)
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
            ++dib;
        }
        // TODO: If V or K are large then we should consider moving them instead of copying
        m_slots[idx].key = key;
        m_slots[idx].value = value;
        m_slots[idx].occupied = true;
        m_slots[idx].DIB = dib;
        ++m_size;
    }

    V *find(K key)
    { // return nullptr if not found
        size_t idx{index(key)};

        while (m_slots[idx].occupied) // backwards shift deletion, so we can stop searching when we hit an empty slot
        {

            if (m_eq(m_slots[idx].key, key))
            {
                return &m_slots[idx].value;
            }
            if (m_slots[idx].DIB == 0) // if the DIB is 0, then we have reached the end of the cluster and the key is not in the map
            {
                return nullptr;
            }
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
        }
        return nullptr;
    }

    void erase(K key)
    {
        size_t idx{index(key)};
        size_t last_idx{};

        while (m_slots[idx].occupied) // backwards shift deletion, so we can stop searching when we hit an empty slot
        {
            if (m_eq(m_slots[idx].key, key))
            {
                m_slots[idx].occupied = false;
                --m_size;
                last_idx = idx;
                break;
            }
            if (m_slots[idx].DIB == 0) // if the DIB is 0, then we have reached the end of the cluster and the key is not in the map
            {
                return;
            }
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
        }

        while (m_slots[idx].occupied && m_slots[idx].DIB > 0) // backwards shift deletion
        {
            m_slots[last_idx] = m_slots[idx];
            m_slots[last_idx].DIB--;
            last_idx = idx;
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
        }
    }

private:
    size_t m_size{0};
    size_t m_capacity{0};
    int m_log_2_cap{0};

    std::vector<slot<K, V>> m_slots{};
    const float m_max_load_factor{0};

    eq m_eq{};
    hash m_hash{};

    static_assert(resize_factor > 1 && (resize_factor & (resize_factor - 1)) == 0 && "resize factor must be > 1, and a power of 2 as hash & (cap -1) is used over %");

    std::size_t index(const K &key) const
    {
        std::uint64_t hash_value = hash{}(key);
        // Fibonachi hashing
        return static_cast<std::size_t>((h * 0x9E3779B97F4A7C15ull) >> (64 - m_log_2_cap)); // returns top m_log_2_cap bits of the hash value, which is equivalent to hash_value % m_capacity when m_capacity is a power of 2
    }
    // allows us to avoid comparasions when rehashing
    void insert_no_check(K &&key, V &&value)
    {
        std::size_t idx{index(key)};
        std::size_t dib{0};
        while (m_slots[idx].occupied)
        {
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
            ++dib;
        }
        m_slots[idx].key = std::move(key);
        m_slots[idx].value = std::move(value);
        m_slots[idx].occupied = true;
        m_slots[idx].DIB = dib;
    }

    void rehash(size_t new_capacity)
    {
        assert((new_capacity & (new_capacity - 1)) == 0 && "new capacity must be a power of 2");
        std::vector<slot<K, V>> old_slots = std::move(m_slots);
        m_slots = std::vector<slot<K, V>>(new_capacity);
        m_capacity = new_capacity;
        m_log_2_cap = static_cast<int>(std::log2(m_capacity));

        for (const auto &s : old_slots)
        {
            if (s.occupied)
            {
                insert_no_check(std::move(s.key), std::move(s.value));
            }
        }
    }
};
