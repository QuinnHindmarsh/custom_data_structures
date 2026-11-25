# custom_data_structures

Header-only data structures in modern C++, each tested and benchmarked against its standard library equivalent.

## Building

g++ with C++23. Each test and benchmark is a single `.cpp` file.

```sh
g++ -std=c++23 -g -O1 hashmap/open_map/open_map_differental_test.cpp -o open_map_test       # tests
g++ -std=c++23 -O2 -DNDEBUG hashmap/open_map/open_map_benchmark.cpp -o open_map_benchmark  # benchmarks
```

## Data structures

| Structure | Header | std equivalent |
|:--|:--|:--|
| [`open_map`](#open_map) | [`hashmap/open_map/open_map.h`](hashmap/open_map/open_map.h) | `std::unordered_map` |
| [`O1vector`](#o1vector) | [`vector/O(1)_vector/O(1)_vector.h`](<vector/O(1)_vector/O(1)_vector.h>) | `std::vector` |

Shared: [`timer/timer.h`](timer/timer.h), a small `steady_clock` stopwatch used by the benchmarks.

---

### open_map

Open addressing hash map using Robin Hood hashing and backward shift deletion.

```cpp
open_map<std::string, int> ages;
ages.insert("alice", 30);          // inserts, or overwrites if present
if (int *age = ages.find("alice")) // nullptr if missing
    *age += 1;
ages.erase("alice");
```

```cpp
open_map<K, V, Hash = std::hash<K>, Eq = std::equal_to<K>,
         resize_factor = 2,         // must be a power of 2
         max_load_factor = 0.75f>
```

#### How it works

- 1 flat vector of slots, each holding:
    - the key and value
    - a 1 byte DIB (distance from initial bucket). 0 = empty, 1 = in its home slot
    - the full hash, for non-integral keys only
- Linear probing, more cache friendly than quadratic probing.
- Linear probing gives longer clusters, partially addressed by Robin Hood hashing and a lower load factor.

#### Fun bits

- **Partial specialisation of the slot.** `slot<K, V>` is specialised for integral keys to drop the stored hash, since hashing and comparing ints is basically free. The map checks for it with `requires(slot_t s) { s.hash; }` and `if constexpr` picks the right path at compile time.
- **Stored hashes skip `==`.** For strings, lookups compare hashes first, so misses almost never touch the key. Rehashing reuses the stored hash.
- **Fibonacci hashing.** The hash is multiplied by 2^64/φ and the top bits pick the slot. `std::hash<int>` is the identity, so without this, nearby ints land in nearby slots.
- **Robin Hood.** On insert, if the carried element is further from home than the one in the slot, they swap. Keeps probe lengths short and even, and lets misses stop early once the slot's DIB is smaller than the probe distance.
- **No tombstones.** Erase shifts following elements back one slot until an empty slot or a DIB of 1. The table ends up as if the key was never inserted.
- **Grows** when load passes `max_load_factor` or a DIB would pass 100 (it has to fit in a byte).

#### Limitations

- Never shrinks.
- No iterators, `size()` or `operator[]`.
- `find`'s pointer is invalidated by any insert or erase.

#### Testing

- [`open_map_differental_test.cpp`](hashmap/open_map/open_map_differental_test.cpp): 1M random ops against `std::unordered_map`, checking `find` agrees after every one.
- [`open_map_check.cpp`](hashmap/open_map/open_map_check.cpp): explicitly instantiates `open_map<std::string, std::string>` so every member compiles.

#### Performance vs `std::unordered_map`

Speedup (higher = `open_map` faster), ~1M keys, each key type at its best load factor:

| Key | LF | insert | update | find hit | find miss | erase miss | erase hit | mixed |
|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| `int32_t` | 0.75 | 3.46x | 1.87x | 2.26x | 2.82x | 2.97x | 12.91x | 3.84x |
| `int64_t` | 0.75 | 3.61x | 1.88x | 2.27x | 2.75x | 3.04x | 12.43x | 3.91x |
| `std::string` | 0.80 | 1.55x | 1.03x | 1.21x | 2.29x | 2.29x | 3.68x | 1.79x |

- Erase hit wins big because `unordered_map` frees a heap node per erase; `open_map` just shifts a few slots.
- String hits/updates are close because hashing and comparing ~24 char keys dominates for both.

Ryzen 5 7640U, g++ 14.2.0, `-O2 -DNDEBUG`, `uint64_t` values. Full results, per-op timings and the load factor sweep: [`open_map_benchmark_results.md`](hashmap/open_map/open_map_benchmark_results.md).

---

### O1vector

A vector where every push and pop is worst case O(1), not amortised O(1). It never stops to copy the whole array.

```cpp
O1vector<int> v;
v.push(5);
v.set(0, 6);       // only existing indices, throws if out of range
int x = v.at(0);   // bounds-checked
int y = v.pop();   // throws if empty
```

#### How it works

- 2 arrays: `m_cur` (always holds every element) and `m_next` (the resize target, `nullptr` when no resize is running).
- At half full, it allocates an array of double the size. Each push or pop then copies 4 elements across, so the copy finishes before `m_cur` fills up.
- At 1/8 full, it starts shrinking to half size the same way. The gap between 1/2 and 1/8 stops it flipping between growing and shrinking.
- `at` only reads `m_cur`. `set` writes to `m_cur`, and also to `m_next` if that index has already been copied.

#### Fun bits

- **Background freer.** `delete[]` on a big array is itself a latency spike, so old arrays of 256 KB or more go to a shared worker thread ([`freer.h`](<vector/O(1)_vector/freer.h>)) that frees them in batches.
- **`set` stays simple.** It only accepts existing indices, so it never has to handle an element that only lives in `m_next`.

#### Limitations

- **Built for cheap types like `int`.** Elements are copied with `std::copy_n`, and `set` writes some elements twice. Large types like `std::string` would need a specialisation that moves elements with `std::move()` and checks which array an element lives in before operating on it.
- **No batch operations.** No range insert, `reserve`, or bulk pop, so filling it is always one element at a time.
- **Small interface.** No iterators, `operator[]`, `emplace`, `shrink_to_fit` or copying.

#### Testing

- [`O1_vec_compare_test.cpp`](<vector/O(1)_vector/O1_vec_compare_test.cpp>): runs the same random ops on an `O1vector` and a `std::vector`, and checks both hold the same elements after every op.
- [`O1_vec_basic_test.cpp`](<vector/O(1)_vector/O1_vec_basic_test.cpp>): a quick smoke test, plus an explicit instantiation so every member compiles.
- [`freer_test.cpp`](<vector/O(1)_vector/freer_test.cpp>): queues thousands of arrays on the freer and checks it drains and shuts down cleanly.

#### Performance vs `std::vector`

`T = int`, same machine as `open_map`. Averaged over 8 runs of the benchmark, each doing 50 runs × 2M ops.

| Op | Mean ns (`O1vector` / `std::vector`) | Mean | Worst-5 avg (`O1vector` / `std::vector`) | Worst-5 |
|:--|--:|:--|--:|:--|
| push | 3.14 / 2.47 | 1.27x slower | 27.0 µs / 384.3 µs | **14.2x better** |
| at | 2.21 / 2.44 | even (noise) | 34.9 µs / 30.2 µs | even (noise) |
| set | 2.11 / 2.24 | even (noise) | 20.9 µs / 20.6 µs | even |
| pop | 2.75 / 0.83 | 3.32x slower | 31.4 µs / 15.8 µs | 1.99x worse |

- **Push trades a bit of speed for no spikes.** `std::vector`'s worst pushes are full copies into a new array. Its per-op stddev is 748 ns, against 113 ns for `O1vector`. The push mean ratio stayed between 1.24x and 1.32x in all 8 runs.
- **at and set match `std::vector`.** Both are one bounds check and one array access. `O1vector` came out slightly ahead on average, but the 95% ranges overlapped in almost every run.
- **Pop is slower because `O1vector` shrinks itself.** `std::vector::pop_back` only decrements the size and never gives memory back. `O1vector` runs its shrink check on every pop, and below 1/8 full it allocates a smaller array and copies 4 elements per pop into it. You pay for the memory being freed.

Benchmark: [`O1_vec_benchmark.cpp`](<vector/O(1)_vector/O1_vec_benchmark.cpp>). Build with `-O2 -DNDEBUG`.
