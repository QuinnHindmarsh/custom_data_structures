#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <functional>
#include <bit>
#include <utility>
#include <concepts>
#include <type_traits>

// REVIEW KEY: every comment from this review starts with TODO(review, <tag>). Tags, most severe first: bug =
//     wrong behaviour today, safety = UB / exception-safety hole, latency = hot-path stall, perf = throughput
//     or memory, api = interface design, style = naming / readability. A NOTE(review) is something done well
//     that a reviewer would call out.
template <typename K, typename Hash, typename Eq>
concept HashableWith = requires(const K &a, const K &b, const Hash &h, const Eq &eq) {
    { h(a) } -> std::convertible_to<std::size_t>;
    { eq(a, b) } -> std::convertible_to<bool>;
};

// TODO(review, perf): `K key{}; V value{};` means every EMPTY slot holds a live, default-constructed K and V.
//     That forces both to be default-constructible, runs a constructor per slot on every rehash (a 2^20 table
//     of std::string runs 2^20 ctors), and erase() never ends an object's lifetime (see erase). Use raw
//     storage (`alignas(K) std::byte key_buf[sizeof(K)]` or a union) and std::construct_at / std::destroy_at
//     driven by DIB, exactly like ring_buffer already does.
//
// TODO(review, perf): sizeof(slot<std::string, int>) is 56, so roughly one slot per 64-byte cache line and
//     every probe step is a likely cache miss. Your storage-refactor idea below is the right direction. It is
//     essentially what absl::flat_hash_map and boost::unordered_flat_map do: a separate dense array of 1-byte
//     control words (DIB or a few hash bits) that you probe, often 16 at a time with SIMD, and only touch the
//     key/value array on a likely match. Benchmark against both, an interviewer will ask how you compare.
// primary template: non-integral keys are assumed large/expensive to hash and compare,
// so the full hash is stored and compared before calling eq, and reused on rehash
template <typename K, typename V>
struct slot
{
    K key{};
    V value{};
    uint64_t hash{};
    uint8_t DIB{0}; // distance to initial bucket - 0 means bucket is empty
};

// integral keys are cheap to hash and compare - don't waste space storing the hash
template <std::integral K, typename V>
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
// TODO(review, api): template parameter naming is inconsistent: `hash`/`eq` are lowercase (and `hash` is also
//     a member name of slot), while K/V are uppercase. Use Hash / KeyEqual like the standard. `const int
//     resize_factor` should be `std::size_t ResizeFactor`: it multiplies a size_t, and an int invites
//     sign-conversion warnings under -Wconversion.
//
// TODO(review, api): missing basics a reviewer will expect: empty(), clear(), contains(k), capacity(),
//     load_factor(), and above all reserve(n). In trading code you size every container at startup so nothing
//     rehashes on the hot path; without reserve the only way is to guess a power of 2 for the ctor.
//
// TODO(review, api): no constructor takes a Hash or Eq instance, so a stateful hasher (e.g. a seeded hash to
//     resist adversarial keys) can't be used. Accept them as ctor arguments and store them with
//     [[no_unique_address]] so empty functors cost 0 bytes.
//
// TODO(review, api): find() returns a V* that is invalidated by ANY later insert (robin hood swaps move
//     elements) or erase (backward shift), not only by a rehash. std::unordered_map guarantees reference
//     stability, so code ported from it will silently break. State the invalidation rules at the top of the
//     class.
//
// TODO(review, bug): the implicitly generated move ctor / move assignment leave the source with an empty
//     m_slots but the old m_capacity, m_shift and m_size. Any use of the moved-from map then reads out of
//     bounds. Confirmed under ASan: `auto b = std::move(a); a.find(1);` segfaults in insertion_idx. A
//     moved-from object must stay valid (contents unspecified, but usable). Write both move ops by hand,
//     reset the source to a valid empty table, and mark them noexcept.
template <typename K, typename V, typename hash = std::hash<K>, typename eq = std::equal_to<K>, const int resize_factor = 2, const float max_load_factor = 0.75f>
    requires HashableWith<K, hash, eq>
class open_map
{
public:
    // TODO(review, safety): the power-of-2 precondition is only an assert, so a release build with capacity 3
    //     silently gives wrong indices, and capacity 1 makes m_shift 64, and `x >> 64` is UB. Round up with
    //     std::bit_ceil (as ring_buffer does) or throw. Rounding up is friendlier: callers usually mean at
    //     least n.
    // Undefined behaviour if capacity is not a power of 2 or less then 2
    explicit open_map(size_t capacity = 2) : m_capacity{capacity}, m_grow_at{static_cast<size_t>(static_cast<float>(capacity) * max_load_factor)}, m_slots(capacity), m_shift{static_cast<uint8_t>(64 - std::countr_zero(capacity))}
    {
        assert(capacity >= 2 && (capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
    };

    // forwarding references: nothing is copied up front.
    //   update -> key is only read (never copied), value is copied/moved into the existing slot
    //   insert -> key and value are copied (lvalue) or moved (rvalue) straight into the new slot
    // TODO(review, api): this is std's insert_or_assign, not insert. std::unordered_map::insert does NOT
    //     overwrite an existing value, so `m.insert(k, v)` will be misread in review. Rename it, and return
    //     something (a bool for inserted vs updated, or a pointer to the stored value) so callers don't need
    //     a second find() after inserting.
    //
    // TODO(review, api): add try_emplace(key, args...) that only constructs V when the key is missing. Today
    //     an expensive V must be fully built by the caller even when the key already exists and the value is
    //     just assigned.
    template <typename KArg, typename VArg>
        requires std::constructible_from<K, KArg> && std::constructible_from<V, VArg> && std::assignable_from<V &, VArg>

    void insert(KArg &&key, VArg &&value)
    {
        // TODO(review, perf): this builds a K for every heterogeneous call, so `insert("some long key", 1)`
        //     on a std::string map heap-allocates even when the key already exists. Support transparent
        //     Hash/Eq (an `is_transparent` member, as in C++20 heterogeneous lookup) so the probe runs on the
        //     string_view and a K is only built when a new slot is actually created.
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

            // NOTE(review): good: building the slot before the rehash is the right fix for the aliasing
            //     use-after-free from the last review. std::vector::push_back(v[0]) has the same problem and
            //     libstdc++ solves it the same way.
            slot_t s = make_slot(std::forward<KArg>(key), std::forward<VArg>(value), h, 1);
            // TODO(review, style): stale comment: the code below checks `probe_distance > max_dib`, not `==
            //     0`. The doc comment on insertion_idx has the same stale claim. Wrong comments cost more
            //     review time than missing ones.
            // probe_distance == 0 means the key would need a DIB > max_dib at existing_idx
            if (m_size + 1 > m_grow_at || probe_distance > max_dib)
            {
                // rehashing makes the existing index and probe distance no longer valid
                rehash(m_capacity * resize_factor);
                insert_no_check(std::move(s), index_from_hash(h));
                ++m_size;
                return;
            }

            s.DIB = probe_distance; // insert_no_check needs the DIB to match idx
            insert_no_check(std::move(s), existing_idx);
            ++m_size;
        }
    }

    // TODO(review, perf): same heterogeneous lookup point as insert. With a std::string key, `m.find(sv)` for
    //     a std::string_view must build a std::string first, which allocates for keys over 15 chars. An
    //     allocation inside a lookup is the first thing a latency reviewer looks for.
    //
    // TODO(review, api): mark find(), size() and similar accessors [[nodiscard]]. Ignoring find's result is
    //     always a bug, and the compiler can tell you.
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

    // TODO(review, perf): a miss probes three times (find, then insert probes again, then find again) and
    //     builds a temporary V{} that insert then moves. Do one probe: call insertion_idx once, return the
    //     matched slot on a hit, otherwise construct V in place at the insert position. Have insert_no_check
    //     return the index where the original element finally landed so you can return a reference to it.
    //
    // TODO(review, api): only takes const K&, so `m[std::move(key)]` copies the key. Add a K&& overload, like
    //     std.
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

    // TODO(review, api): return bool (or size_t like std::unordered_map::erase) so the caller knows whether
    //     anything was removed. Mark it [[nodiscard]] only if you return bool, since std callers often ignore
    //     the count.
    //
    // TODO(review, style): the first loop below re-implements insertion_idx. Reuse it so there is exactly one
    //     probe loop to optimise, test and keep correct.
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
        // TODO(review, perf): erase never destroys the erased key/value. If the next slot has DIB <= 1, the
        //     loop below doesn't run and the erased slot is just marked DIB = 0 with its K and V still fully
        //     alive (an erased 1 MB std::string stays allocated until the next rehash or ~open_map). When the
        //     loop does run, the last slot keeps a moved-from object, which is fine for std types but not for
        //     a type whose move is a copy. A container should end the lifetime of what it erases: with raw
        //     storage this becomes std::destroy_at(&slot.key) etc.
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

    // TODO(review, api): not const, so it can't be called on a `const open_map&`. Should be `[[nodiscard]]
    //     size_t size() const noexcept`. Same noexcept point for empty() once it exists.
    size_t size()
    {
        return m_size;
    }

private:
    static_assert(resize_factor > 1 && (resize_factor & (resize_factor - 1)) == 0 && "resize factor must be > 1, and a power of 2 as hash & (cap -1) is used over %");
    static_assert(max_load_factor > 0.0f && max_load_factor < 1.0f && "max load factor must be between 0 and 1");
    // TODO(review, perf): m_capacity duplicates m_slots.size(), and every probe computes `& (m_capacity -
    //     1)`. Store m_mask instead (ring_buffer already does). Group the members the lookup path reads
    //     (m_slots data pointer, m_shift, m_mask) together at the top. sizeof(open_map<int, int>) is 56
    //     today; it can be 32 with [[no_unique_address]] on the functors and dropping redundant fields.
    size_t m_capacity{};
    size_t m_size{0};
    size_t m_grow_at;
    // TODO(review, style): npos is never used. Remove dead code before review so reviewers don't hunt for its
    //     purpose.
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
        // TODO(review, style): the trailing comment is wrong: taking the top bits of hash * 2^64/phi is not
        //     equivalent to hash % m_capacity, it is a different (better mixed) mapping. That mixing is the
        //     whole point of Fibonacci hashing: it rescues identity hashes like std::hash<int> from
        //     clustering on keys that share low bits. Say that instead.
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

    // TODO(review, safety): not exception safe. m_slots is moved out BEFORE the new vector is allocated, so
    //     if `std::vector(new_capacity)` throws bad_alloc (or a K/V default ctor throws), the map is left
    //     with empty m_slots but the old m_size and m_capacity, and the next find reads out of bounds. Build
    //     the new vector into a local first, then swap it in. Moves inside the loop also need K and V to be
    //     nothrow-move-constructible: static_assert that, or copy when the move may throw
    //     (std::move_if_noexcept).
    //
    // TODO(review, latency): a rehash runs inside insert(), on whoever's hot path triggered it. For a 1M-slot
    //     table that is milliseconds of allocation, page faults and moves in one call, which is the latency
    //     spike trading reviewers care about most. Mitigations, cheapest first: reserve() at startup, then an
    //     incremental rehash that moves a few slots per operation (the trick you already use in O1vector).
    void rehash(size_t new_capacity)
    {
        assert((new_capacity & (new_capacity - 1)) == 0 && "new capacity must be a power of 2");
        std::vector<slot<K, V>> old_slots = std::move(m_slots);
        m_slots = std::vector<slot<K, V>>(new_capacity);
        m_capacity = new_capacity;
        m_grow_at = static_cast<size_t>(static_cast<float>(m_capacity) * max_load_factor);
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
        // TODO(review, style): unreachable after `while (true)`. Delete it.
        return {idx, probe_distance};
    }
};
