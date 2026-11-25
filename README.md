# custom_data_structures

Data structures written from scratch in modern C++. Each one is header-only, tested against its standard library equivalent, and benchmarked against it.

## Building

Everything needs g++ with C++23. Each test and benchmark is a single `.cpp` file you compile directly.

```sh
# tests: debug info on, light optimisation
g++ -std=c++23 -g -O1 hashmap/open_map/open_map_differental_test.cpp -o open_map_test

# benchmarks: always build with optimisation and asserts off, -O0 timings mean nothing
g++ -std=c++23 -O2 -DNDEBUG hashmap/open_map/open_map_benchmark.cpp -o open_map_benchmark
```

---

## Data structures

| Structure | Header | Standard library equivalent | Status |
|:--|:--|:--|:--|
| [`open_map`](#open_map) | [`hashmap/open_map/open_map.h`](hashmap/open_map/open_map.h) | `std::unordered_map` | Working, tested, benchmarked |

Shared helpers:

- [`timer/timer.h`](timer/timer.h): a small `std::chrono::steady_clock` stopwatch the benchmarks use.

---

### open_map

A header-only hash map that uses open addressing with **Robin Hood hashing** and **backward shift deletion**.

#### Usage

```cpp
#include "open_map.h"

open_map<std::string, int> ages;

ages.insert("alice", 30);   // adds the key
ages.insert("alice", 31);   // key already there: overwrites the value

if (int *age = ages.find("alice")) // nullptr if the key is missing
    *age += 1;                     // the pointer lets you change the value in place

ages.erase("alice");        // erasing a missing key does nothing
```

Template parameters:

```cpp
open_map<K, V,
         Hash = std::hash<K>,
         Eq = std::equal_to<K>,
         resize_factor = 2,        // capacity multiplier on growth, must be a power of 2
         max_load_factor = 0.75f>  // grow once size > capacity * max_load_factor
```

#### How it works
- 1 flat vector containing slots
     - each slot holds:
        - The key
        - The value
        - a one byte DIB (distance from inital bucket), how far the key is from where it is hashed to. 0 represents the slot is empty, and thus 1 means it sits in its home slot
        - For non integral keys the full hash.
- Linear probing, so more cache friendly then quadratic probing.
- Linear probing can have longer clusters then quadratic probing, this is partially addressed through a lower load factor and robin hood hashing

**2. Picking the home slot: Fibonacci hashing.**
Capacity is always a power of 2. The hash is multiplied by `0x9E3779B97F4A7C15` (2^64 / φ), and the **top** bits of the result give the slot index. This spreads out weak hashes. That matters because `std::hash<int>` is the identity function, so close-together integer keys would otherwise land in close-together slots.

**3. Collisions: linear probing + Robin Hood.**
If the home slot is taken, the map checks the next slot, then the next, wrapping around at the end. While inserting, it carries the new element forward and compares DIBs:

> If the element being carried is further from home than the one sitting in the slot, they swap. The displaced element is carried on instead.

This "take from the rich, give to the poor" rule keeps probe lengths short and even. No element ends up far from home while others sit right next to theirs.

```
insert C (home = slot 0)                        [key DIB]

slot:    0      1      2      3      4
before: [A 1]  [B 1]  [D 1]  [    ]  [    ]

slot 0: carrying C (DIB 1) vs A (1)  -> not further from home, keep going
slot 1: carrying C (DIB 2) vs B (1)  -> C is further from home: C takes slot 1, now carry B
slot 2: carrying B (DIB 2) vs D (1)  -> B is further from home: B takes slot 2, now carry D
slot 3: carrying D (DIB 2), empty    -> place D

after:  [A 1]  [C 2]  [B 2]  [D 2]  [    ]
```

Without the swaps, C would have ended up in slot 3 with DIB 4 while A, B and D all sat at DIB 1.

**4. Lookups stop early.**
Robin Hood keeps one rule true at all times: walking along a run of filled slots, the DIB can grow by at most 1 per step. So if a lookup reaches a slot whose DIB is smaller than its own probe distance, the key can't be any further along, and the lookup stops. A miss doesn't have to scan to the next empty slot.

**5. Erase: backward shift deletion.**
Many open-addressing maps erase by leaving a "tombstone" marker, which slows later lookups down. This map has no tombstones. After removing an element, every following element with `DIB > 1` moves back one slot and has its DIB reduced by 1. The shifting stops at an empty slot or at an element already in its home slot. The table ends up exactly as if the erased key had never been inserted.

**6. Storing the hash only when it helps.**
The slot layout depends on the key type:
- **Integral keys:** hashing and comparing are almost free, so the hash isn't stored. That keeps slots small.
- **Other keys (e.g. `std::string`):** the full 64-bit hash is stored. Lookups compare hashes first and only call `==` when the hashes match. Rehashing reuses the stored hash instead of hashing every key again.

**7. Growing.**
The table doubles in size (`resize_factor`) when either of these happens:
- the size would go past `capacity * max_load_factor`
- a probe distance would go past 100. A 1-byte DIB can't hold huge distances, and long probe chains mean the hash is distributing badly.

On growth, every element is reinserted into the new array.

#### Limitations

- The table never shrinks. Erasing elements doesn't free memory.
- No iterators, no `size()`, no `operator[]`. The API is `insert` / `find` / `erase`.
- `find` returns a raw pointer that is only valid until the next insert or erase. Both can move elements.

#### Testing

- [`open_map_differental_test.cpp`](hashmap/open_map/open_map_differental_test.cpp) applies 1M random inserts, updates, finds and erases to both `open_map` and `std::unordered_map`. After every operation it checks that `find` gives the same answer for both. It also checks the const and non-const `find` overloads, lookups with temporary keys, and writing through the returned pointer.
- [`open_map_check.cpp`](hashmap/open_map/open_map_check.cpp) explicitly instantiates `open_map<std::string, std::string>`. That forces every member function to compile, including ones no test calls.

#### Performance vs `std::unordered_map`

`open_map` is faster than `std::unordered_map` in every phase, for every key type tested. Each row uses that key type's best load factor.

**Speedup** (`unordered_map` time ÷ `open_map` time, above 1x means `open_map` is faster):

| Key type | Load factor | insert | update | find hit | find miss | erase miss | erase hit | mixed |
|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| `int32_t` | 0.75 | 3.46x | 1.87x | 2.26x | 2.82x | 2.97x | 12.91x | 3.84x |
| `int64_t` | 0.75 | 3.61x | 1.88x | 2.27x | 2.75x | 3.04x | 12.43x | 3.91x |
| `std::string` | 0.80 | 1.55x | 1.03x | 1.21x | 2.29x | 2.29x | 3.68x | 1.79x |

**Time per operation in ns** (`open_map` / `unordered_map`, lower is better):

| Phase | `int32_t` | `int64_t` | `std::string` |
|:--|--:|--:|--:|
| insert | 72.1 / 249.7 | 63.3 / 228.7 | 276.5 / 428.0 |
| update | 26.5 / 49.6 | 25.9 / 48.6 | 161.1 / 165.8 |
| find hit | 17.2 / 38.9 | 16.8 / 38.1 | 220.4 / 266.4 |
| find miss | 16.4 / 46.2 | 16.1 / 44.3 | 69.3 / 158.5 |
| erase miss | 16.7 / 49.5 | 16.0 / 48.7 | 70.1 / 160.3 |
| erase hit | 18.1 / 233.5 | 17.7 / 219.9 | 175.1 / 644.0 |
| mixed | 40.5 / 155.8 | 38.4 / 150.2 | 183.7 / 328.0 |

What stands out:
- **Erase hit is the biggest win (12–13x for integers).** `std::unordered_map` frees a heap node on every erase. `open_map` just shifts a few neighbouring slots back.
- **Misses are about 2.3–3x faster.** Robin Hood lookups stop early (step 4), and string misses almost never call `==` because the stored hashes don't match (step 6).
- **String updates and hits are close to even (1.03x, 1.21x).** Most of that time goes to hashing and comparing the ~24-character keys, which both maps have to do.

Test setup: AMD Ryzen 5 7640U, g++ 14.2.0, `-O2 -DNDEBUG`, value type `uint64_t`. String keys are about 24 characters, too long for the small-string optimisation. Full results, including the load factor sweep, are in [`open_map_benchmark_results.md`](hashmap/open_map/open_map_benchmark_results.md).

##### How it was measured

[`open_map_benchmark.cpp`](hashmap/open_map/open_map_benchmark.cpp) measures each key type in two stages:

1. **Sweep:** every load factor from 0.70 to 0.90 runs against `std::unordered_map`, with 20M ops per phase (5M for strings). The best load factor is the one with the lowest geometric mean time across all phases.
2. **Final:** that load factor runs against `std::unordered_map` with 100M ops per phase (50M for strings). These are the numbers above.

The map holds about 1M keys the whole time. More ops come from repeating 1M-op passes, not from a bigger map. Passes alternate between the two maps, and each reported time is the median pass. Every run passed the correctness check: both maps found the same values and the expected number of hits.

Phases:
- **insert:** N new keys into an empty map, including every resize along the way.
- **update:** the same N keys again, overwriting the values.
- **find hit / find miss:** lookups of keys that are in the map / not in the map.
- **erase miss / erase hit:** erases of keys that are not in the map / every key in the map.
- **mixed:** random ops on a map that already holds N keys: 25% insert, 25% update, 25% find (half hits, half misses), 25% erase (half hits, half misses).
