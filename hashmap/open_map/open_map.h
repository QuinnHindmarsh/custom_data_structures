#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <functional>
#include <bit>
#include <utility>
#include <concepts>
#include <type_traits>

template <typename T>
concept Hashable = requires(T a, T b) {
    { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
    { a == b } -> std::convertible_to<bool>;
};

template <typename T>
concept IntegralKey = Hashable<T> && std::integral<T>;

// primary template: non-integral keys are assumed large/expensive to hash and compare,
// so the full hash is stored and compared before calling eq, and reused on rehash
template <Hashable K, typename V>
struct slot
{
    K key{};
    V value{};
    uint64_t hash{};
    uint8_t DIB{0}; // distance to initial bucket - 0 means bucket is empty
};

// integral keys are cheap to hash and compare - don't waste space storing the hash
template <IntegralKey K, typename V>
struct slot<K, V>
{
    K key{};
    V value{};
    uint8_t DIB{0}; // distance to initial bucket - 0 means bucket is empty
};

/*
TODO:

improvments
    - itterators

optimisations/optimisations to consdier
    - My own hash function?
    - Quadratic probing would reduce clustering, but has worse cache locality,
        and clustering is already somewhat addressed with robin hood hashing, and low load factor
    - compare to using second hashing?
    - refactoring storage
    - auto adjust max_load_factor based on size of K and V?



storage refactor idea:
struct mp{
    vector<pair<key, DIB>> keys;
    vector<pair<hash,value>>P values;

    // keys[i].first is key for values[i].second
    }
I am mainly checking DIB and key when itterating, this would allow more keys and DIBs to exist in one cache line. Then when the matching key is found it requires one additonal memory jump

*/
template <Hashable K, typename V, typename hash = std::hash<K>, typename eq = std::equal_to<K>, const int resize_factor = 2, const float max_load_factor = 0.75f>
class open_map
{
public:
    // Undefined behaviour if capacity is not a power of 2 or less then 2
    explicit open_map(size_t capacity = 2) : m_capacity{capacity}, m_grow_at{static_cast<size_t>(capacity * max_load_factor)}, m_slots(capacity), m_shift{static_cast<uint8_t>(64 - std::countr_zero(capacity))}
    {
        assert(capacity >= 2 && (capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
    };

    // forwarding references: nothing is copied up front.
    //   update -> key is only read (never copied), value is copied/moved into the existing slot
    //   insert -> key and value are copied (lvalue) or moved (rvalue) straight into the new slot
    template <typename KArg, typename VArg>
        requires std::constructible_from<K, KArg> && std::constructible_from<V, VArg> && std::assignable_from<V &, VArg>
    void insert(KArg &&key, VArg &&value)
    {
        if constexpr (!std::same_as<std::remove_cvref_t<KArg>, K>)
        {
            // different type, e.g. const char* for a std::string key. Build the K once here,
            // otherwise m_hash and m_eq would each make their own temporary K
            insert(K(std::forward<KArg>(key)), std::forward<VArg>(value));
        }
        else
        {
            const uint64_t h = m_hash(key);
            auto [existing_idx, probe_distance] = insertion_idx(key, h);
            if (slot_matches(m_slots[existing_idx], key, h))
            {
                m_slots[existing_idx].value = std::forward<VArg>(value);
                return;
            }

            // probe_distance == 0 means the key would need a DIB > max_dib at existing_idx
            if (m_size + 1 > m_grow_at || probe_distance > max_dib)
            {
                // rehashing makes the existing index and probe distance no longer valid
                rehash(m_capacity * resize_factor);
                insert_no_check(make_slot(std::forward<KArg>(key), std::forward<VArg>(value), h, 1), index_from_hash(h));
                ++m_size;
                return;
            }

            insert_no_check(make_slot(std::forward<KArg>(key), std::forward<VArg>(value), h, probe_distance), existing_idx);
            ++m_size;
        }
    }

    V *find(const K &key) // return nullptr if not found
    {
        return const_cast<V *>(std::as_const(*this).find(key));
    }

    const V *find(const K &key) const
    {
        const uint64_t h = m_hash(key);
        auto [idx, _probe_cnt] = insertion_idx(key, h);
        if (slot_matches(m_slots[idx], key, h))
        {
            return &m_slots[idx].value;
        }
        return nullptr;
    }

    // like std::unordered_map: inserts a default constructed V if key is missing.
    V &operator[](const K &key)
        requires std::default_initializable<V>
    {
        V *v = find(key);
        if (v)
            return *v;
        insert(key, V{});
        return *find(key); // insert can swap/rehash, so look the slot up again
    }

    void erase(const K &key)
    {
        const uint64_t h = m_hash(key);
        size_t idx{index_from_hash(h)};
        uint8_t probe_distance{1}; // larger then needed, but the extra byte removes a comparasion each itteration

        while (true) // backwards shift deletion, so we can stop searching when we hit an empty slot
        {
            if (m_slots[idx].DIB == 0)
                return;
            if (slot_matches(m_slots[idx], key, h))
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

    size_t size()
    {
        return m_size;
    }

private:
    static_assert(resize_factor > 1 && (resize_factor & (resize_factor - 1)) == 0 && "resize factor must be > 1, and a power of 2 as hash & (cap -1) is used over %");
    static_assert(max_load_factor > 0.0f && max_load_factor < 1.0f && "max load factor must be between 0 and 1");
    size_t m_capacity{};
    size_t m_size{0};
    size_t m_grow_at;
    static constexpr std::size_t npos{static_cast<std::size_t>(-1)}; // max_size_t - used as a sentinal value
    static constexpr uint8_t max_dib{100};                           // longest allowed probe distance, exceeding it triggers a rehash
    std::vector<slot<K, V>> m_slots{};
    uint8_t m_shift;

    eq m_eq{};
    hash m_hash{};

    using slot_t = slot<K, V>;

    // true when the slot specialisation stores the hash (i.e. key is assumed large / expensive to compare)
    static constexpr bool stores_hash = requires(slot_t s) { s.hash; };

    size_t index_from_hash(uint64_t hash_value) const
    {
        // Fibonachi hashing
        return static_cast<size_t>((hash_value * 0x9E3779B97F4A7C15ull) >> m_shift); // returns top log2(m_capacity) bits of the hash value, which is equivalent to hash_value % m_capacity when m_capacity is a power of 2
    }

    // occupied + same key. When the hash is stored, compare it first so the (expensive) eq is
    // only called on a likely match
    bool slot_matches(const slot_t &s, const K &key, uint64_t h) const
    {
        if (s.DIB == 0)
            return false;
        if constexpr (stores_hash)
        {
            if (s.hash != h)
                return false;
        }
        return m_eq(s.key, key);
    }

    // hash of an already stored slot - free when stored, recomputed otherwise
    uint64_t slot_hash(const slot_t &s) const
    {
        if constexpr (stores_hash)
            return s.hash;
        else
            return m_hash(s.key);
    }

    // this is the single point where the key/value are copied or moved into the map.
    // K(...)/V(...) instead of bare braces so e.g. an int value for a double V isn't a narrowing error
    template <typename KArg, typename VArg>
    static slot_t make_slot(KArg &&key, VArg &&value, [[maybe_unused]] uint64_t h, uint8_t dib)
    {
        if constexpr (stores_hash)
            return slot_t{K(std::forward<KArg>(key)), V(std::forward<VArg>(value)), h, dib};
        else
            return slot_t{K(std::forward<KArg>(key)), V(std::forward<VArg>(value)), dib};
    }

    // robin hood insert starting at idx, cur.DIB must already be the probe distance for idx.
    // no key comparisons - caller guarantees the key is not present.
    // returns false if the element being carried would need a DIB > max_dib. The table is still
    // valid in that case (every slot written so far has a correct DIB), but cur now holds an
    // element (possibly a displaced one, not the original) that has not been placed yet
    [[nodiscard]] bool try_insert_no_check(slot_t &cur, size_t idx)
    {
        while (m_slots[idx].DIB > 0)
        {
            if (cur.DIB > m_slots[idx].DIB)
            {
                std::swap(cur, m_slots[idx]);
            }
            if (cur.DIB == max_dib)
                return false;

            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
            ++cur.DIB;
        }
        m_slots[idx] = std::move(cur);
        return true;
    }

    // as above, but grows the table until the carried element fits.
    // can recurse through rehash -> insert_no_check -> rehash, which is safe: the inner rehash
    // only touches m_slots, and the outer rehash's loop reads its own old_slots + the updated members
    void insert_no_check(slot_t &&cur, size_t idx)
    {
        while (!try_insert_no_check(cur, idx))
        {
            rehash(m_capacity * resize_factor);
            cur.DIB = 1;
            idx = index_from_hash(slot_hash(cur));
        }
    }

    void rehash(size_t new_capacity)
    {
        assert((new_capacity & (new_capacity - 1)) == 0 && "new capacity must be a power of 2");
        std::vector<slot<K, V>> old_slots = std::move(m_slots);
        m_slots = std::vector<slot<K, V>>(new_capacity);
        m_capacity = new_capacity;
        m_grow_at = static_cast<size_t>(m_capacity * max_load_factor);
        m_shift = static_cast<uint8_t>(64 - std::countr_zero(m_capacity));

        for (auto &s : old_slots)
        {
            if (s.DIB)
            {
                const size_t idx = index_from_hash(slot_hash(s)); // stored hash reused, no rehashing of the key
                s.DIB = 1;
                insert_no_check(std::move(s), idx);
            }
        }
    }

    // returns {idx, probe distance at idx}. A probe distance of 0 means the key is not present and
    // inserting it would need a DIB > max_dib (the slot at idx is occupied, so find still works)
    std::pair<size_t, uint8_t> insertion_idx(const K &key, uint64_t h) const
    {
        size_t idx{index_from_hash(h)};
        uint8_t probe_distance{1};

        while (true) // backwards shift deletion, so we can stop searching when we hit an empty slot
        {
            if (m_slots[idx].DIB < probe_distance) // if the DIB is 1, then we have reached the end of the cluster and the key is not in the map
            {
                return {idx, probe_distance};
            }
            if (slot_matches(m_slots[idx], key, h))
            {
                return {idx, probe_distance};
            }

            ++probe_distance;
            idx = (idx + 1) & (m_capacity - 1); // linear probing, wrap around using bitwise AND since capacity is a power of 2
        }
        return {idx, probe_distance};
    }
};
