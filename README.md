# custom_data_structures

Header-only data structures in modern C++, each tested and benchmarked against its standard library equivalent.

## Building

g++ with C++23. Each test and benchmark is a single `.cpp` file. Run these from the repo root.

Tests, built with debug info and light optimisation:

```sh
g++ -std=c++23 -g -O1 hashmap/open_map/open_map_differental_test.cpp -o hashmap/open_map/open_map_differental_test.exe
./hashmap/open_map/open_map_differental_test.exe

g++ -std=c++23 -g -O1 "vector/O(1)_vector/O1_vec_compare_test.cpp" -o "vector/O(1)_vector/O1_vec_compare_test.exe"
"./vector/O(1)_vector/O1_vec_compare_test.exe"

g++ -std=c++23 -g -O1 deque/ring_buffer/ring_buffer_comparasion_tests.cpp -o deque/ring_buffer/ring_buffer_comparasion_tests.exe
./deque/ring_buffer/ring_buffer_comparasion_tests.exe
```

Benchmarks use [Google Benchmark](https://github.com/google/benchmark). Install it once. In an MSYS2 shell (UCRT64 or MINGW64, whichever your g++ comes from):

```sh
pacman -S $MINGW_PACKAGE_PREFIX-benchmark
```

On Ubuntu/Debian it's `sudo apt install libbenchmark-dev`.

Build with `-O2 -DNDEBUG` (unoptimised timings are meaningless) and link `-lbenchmark`. On Windows also link `-lshlwapi`. On Linux use `-lpthread` instead:

```sh
g++ -std=c++23 -O2 -DNDEBUG hashmap/open_map/open_map_benchmark.cpp -o hashmap/open_map/open_map_benchmark.exe -lbenchmark -lshlwapi
g++ -std=c++23 -O2 -DNDEBUG "vector/O(1)_vector/O1_vec_benchmark.cpp" -o "vector/O(1)_vector/O1_vec_benchmark.exe" -lbenchmark -lshlwapi
g++ -std=c++23 -O2 -DNDEBUG deque/ring_buffer/ring_buffer_benchmark.cpp -o deque/ring_buffer/ring_buffer_benchmark.exe -lbenchmark -lshlwapi
```

Quick check that a benchmark builds and runs (a few seconds, numbers are rough):

```sh
./deque/ring_buffer/ring_buffer_benchmark.exe --benchmark_min_time=0.01s
```

Full run. Repeats every benchmark 20 times, reports mean / median / stddev / cv, and interleaves the repetitions of the two containers so a noisy moment hits both:

```sh
./deque/ring_buffer/ring_buffer_benchmark.exe --benchmark_repetitions=20 --benchmark_report_aggregates_only=true --benchmark_enable_random_interleaving=true
```

Useful flags (they work on every benchmark):

| flag | does |
|:--|:--|
| `--benchmark_filter=<regex>` | run only matching benchmarks, e.g. `--benchmark_filter=std::string` or `--benchmark_filter=^insert/` |
| `--benchmark_min_time=2s` | run each benchmark for at least this long (default 0.5s) |
| `--benchmark_out=results.json --benchmark_out_format=json` | also save results to a file (`csv` works too) |
| `--benchmark_counters_tabular=true` | put counters (`per_op`, `p99.9_ns`, ...) in aligned columns |
| `--benchmark_list_tests=true` | list benchmark names without running them |

Benchmarks are named `<op>/<type>/<container>`, so each pair sits next to each other. `per_op` is the time of one call. Each file's header comment says exactly what every op measures. To build with a smaller data size for a fast smoke test, add `-DBENCH_N=4096`.

The results tables below were measured with the earlier hand-written benchmark harness, before the move to Google Benchmark.

## Data structures

| Structure | Header | std equivalent |
|:--|:--|:--|
| [`open_map`](#open_map) | [`hashmap/open_map/open_map.h`](hashmap/open_map/open_map.h) | `std::unordered_map` |
| [`O1vector`](#o1vector) | [`vector/O(1)_vector/O(1)_vector.h`](<vector/O(1)_vector/O(1)_vector.h>) | `std::vector` |
| [`ring_buffer`](#ring_buffer) | [`deque/ring_buffer/ring_buffer.h`](deque/ring_buffer/ring_buffer.h) | `std::deque` |

Shared: 
- [`shared_utility/timer.h`](shared_utility/timer.h), a small `steady_clock` stopwatch.
- [`shared_utility/random.h`](shared_utility/random.h), basic namepsace allowing for the generation of random numbers. Used for testing purposes.
- [`shared_utility/freer.h`](shared_utility/freer.h), a background thread that frees large arrays in batches. Used by `O1vector`.


---

### open_map

Open addressing hash map using Robin Hood hashing and backward shift deletion.

```cpp
open_map<std::string, int> ages;
ages.insert("alice", 30);          // inserts, or overwrites if present
if (int *age = ages.find("alice")) // nullptr if missing
    *age += 1;
ages["bob"] += 5;                  // like std::unordered_map: inserts 0 first if missing
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
- No iteratorsor.
- `operator[]` needs a default constructible `V`, and only takes a `K` (no `const char*` shortcut like `insert` has).
- The pointer from `find` and the reference from `[]` are invalidated by any insert or erase, including a `[]` on a missing key.

#### Testing

- [`open_map_differental_test.cpp`](hashmap/open_map/open_map_differental_test.cpp): 1M random ops (insert, erase, find, update through `find`, read and write with `[]`) against `std::unordered_map`, checking `find` agrees after every one. `[]` reads also check the returned value matches and the reference points at the same slot as `find`.

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
int z = v[0];      // not bounds-checked, read-only
int y = v.pop();   // throws if empty
```

Bad calls throw `std::out_of_range`, like `std::vector::at`. The message names the call, the index and the size:

```
O1vector::at(3): index out of range, size is 3 (valid indices are 0 to 2)
O1vector::set(0): index out of range, the vector is empty
O1vector::pop(): can't pop, the vector is empty
```

#### How it works

- 2 arrays: `m_cur` (always holds every element) and `m_next` (the resize target, `nullptr` when no resize is running).
- At half full, it allocates an array of double the size. Each push or pop then copies 4 elements across, so the copy finishes before `m_cur` fills up.
- At 1/8 full, it starts shrinking to half size the same way. The gap between 1/2 and 1/8 stops it flipping between growing and shrinking.
- `at` and `[]` only read `m_cur`. `set` writes to `m_cur`, and also to `m_next` if that index has already been copied.

#### Fun bits

- **Background freer.** `delete[]` on a big array is itself a latency spike, so old arrays of 256 KB or more go to a shared worker thread ([`freer.h`](shared_utility/freer.h)) that frees them in batches.
- **`set` stays simple.** It only accepts existing indices, so it never has to handle an element that only lives in `m_next`.
- **`[]` is read-only.** It returns `const T&`. A writable `T&` would write only to `m_cur`, so mid-resize the copy in `m_next` would go stale. All writes go through `set`.
- **Cheap checks, cold messages.** The bounds checks inline into the hot path. The error message is only built in a separate `[[noreturn]]` function, which only runs once something has gone wrong.

#### Limitations

- **Built for cheap types like `int`.** Elements are copied with `std::copy_n`, and `set` writes some elements twice. Large types like `std::string` would need a specialisation that moves elements with `std::move()` and checks which array an element lives in before operating on it.
- **No batch operations.** No range insert, `reserve`, or bulk pop, so filling it is always one element at a time.
- **Small interface.** No iterators, writable `operator[]`, `emplace`, `shrink_to_fit` or copying.

#### Testing

- [`O1_vec_compare_test.cpp`](<vector/O(1)_vector/O1_vec_compare_test.cpp>): runs the same random ops on an `O1vector` and a `std::vector`, and checks both hold the same elements after every op, read through `at`, `at_ptr` and `[]`. Also checks `[]` mid-grow and mid-shrink, and the exact exception type and message for every bad call.

#### Performance vs `std::vector`

`T = int`, same machine as `open_map`, g++ 16.1.0. Averaged over 5 runs of the benchmark, each doing 50 runs × 2M ops.

| Op | Mean ns ± noise (`O1vector` / `std::vector`) | Mean | Worst-5 avg (`O1vector` / `std::vector`) | Worst-5 |
|:--|--:|:--|--:|:--|
| push | 2.11 ±0.05 / 1.61 ±0.04 | 1.31x slower | 16.8 µs / 261.2 µs | **15.5x better** |
| at | 1.73 ±0.14 / 1.70 ±0.14 | even (noise) | 20.0 µs / 18.9 µs | even |
| set | 1.46 ±0.11 / 1.38 ±0.09 | even (noise) | 14.7 µs / 14.5 µs | even |
| pop | 1.76 ±0.04 / 0.44 ±0.003 | 4.0x slower | 20.4 µs / 9.6 µs | 2.13x worse |

Spread of single ops, each timed on its own with `rdtsc`:

| Op | Stddev ns (`O1vector` / `std::vector`) | Variance ns² (`O1vector` / `std::vector`) |
|:--|--:|--:|
| push | 54.1 / **507.8** | 2,926 / **257,876** |
| at | 74.4 / 68.5 | 5,539 / 4,696 |
| set | 46.0 / 52.8 | 2,113 / 2,783 |
| pop | 54.9 / 22.5 | 3,018 / 507 |

- **Noise** is the benchmark's 95% range on the mean, averaged over the 5 runs. If the two ranges overlap, the difference may be noise.
- **Stddev** is the square root of the averaged variance.
- **Push trades a bit of speed for no spikes.** `std::vector`'s worst pushes are full copies into a new array, which gives it about 9x the stddev and 88x the variance of `O1vector`. The push mean ratio stayed between 1.29x and 1.34x in all 5 runs.
- **at and set match `std::vector`.** Both are one bounds check and one array access. The 95% ranges overlapped in every run. `O1vector` was ahead in 2 of 5 runs for `at` and 1 of 5 for `set`.
- **Pop is slower because `O1vector` shrinks itself.** `std::vector::pop_back` only decrements the size and never gives memory back. `O1vector` runs its shrink check on every pop, and below 1/8 full it allocates a smaller array and copies 4 elements per pop into it. You pay for the memory being freed.

Benchmark: [`O1_vec_benchmark.cpp`](<vector/O(1)_vector/O1_vec_benchmark.cpp>). Build with `-O2 -DNDEBUG`.

---

### ring_buffer

A double-ended queue in one flat, growable array. Push and pop at both ends, and random access with `[]`.

```cpp
ring_buffer<int> q;      // starting capacity is rounded up to a power of 2
q.reserve(1000);         // never shrinks, asking for less does nothing
q.push_back(1);
q.push_front(0);
q.emplace_back(2);
int a = q[1];            // 1, not bounds-checked
q.front() = 5;           // front() and back() return references
int b = q.pop_back_return();  // reads and pops in one call (std::deque needs back() then pop_back())
q.pop_front();
```

Out of bounds access and popping an empty buffer are undefined behaviour, the same as `std::deque`.

#### How it works

- 1 array whose capacity is always a power of 2, so the slot for index `i` is `i & mask` instead of `i % capacity`.
- `m_head` and `m_tail` are never wrapped back into range. They are plain `size_t` counters, masked only when the array is accessed. `push_front` decrements `m_head`, and `size()` is just `m_tail - m_head`. Unsigned wraparound keeps this correct even when `m_head` goes "below 0".
- When full, it doubles. The contents can wrap past the end of the array, so they are copied in 2 pieces (head to the end, then the start to tail) into the new array starting at slot 0.

#### Fun bits

- **Pre-touched pages.** The OS only maps a page of RAM on its first write. `push_front` fills the array from the back, so it would fault pages in backwards one at a time, about 2x slower. `grow` writes 1 byte per 4 KB page, front to back, first.
- **`memcpy` when it can.** `if constexpr (std::is_trivially_copyable_v<T>)` picks `memcpy` for types like `int`, and `uninitialized_move` plus `destroy` for everything else.
- **Small hot path.** The full check is `[[unlikely]]` and `grow` is `[[gnu::noinline]]`, so a push inlines to a compare, a construct and an increment.
- **Take in one call.** `pop_back_return` and `pop_front_return` move the element out and pop it, so there's no separate `back()` read.

#### Limitations

- Never shrinks.
- No iterators and no `.at`.
- No move constructor or move assignment. Declaring the copy constructor turned off the implicit ones, so `std::move(q)` copies the whole buffer.
- Copy assignment always allocates a new array, even when the old one is big enough.
- Any grow invalidates every reference. `std::deque` keeps references valid on pushes at either end.

#### Testing

- [`ring_buffer_comparasion_tests.cpp`](deque/ring_buffer/ring_buffer_comparasion_tests.cpp): random ops (push and pop at both ends, take, writes through `[]`, `reserve`, copy construct, copy assign, self-assignment) against `std::deque`, for `int32_t`, `uint64_t` and `std::string`. Every element is compared every 100 ops and after every `reserve` or copy. Each round starts at a random capacity, including 0 and non powers of 2.
  - A `Tracked` type counts live objects, so a leaked or double-destroyed element fails the test. It isn't trivially copyable, so it also covers the `uninitialized_move` path in `grow`.

#### Performance vs `std::deque`

Speedup (higher = `ring_buffer` faster). Same machine, g++ 16.1.0, `-O2 -DNDEBUG`. Averaged over 20 runs of the benchmark, each reporting the median of 20 passes × 1M ops. The range is the lowest and highest speedup out of the 20 runs.

| Op | `int32_t` | `int64_t` | `std::string` |
|:--|--:|--:|--:|
| push_back | 0.88x (0.84–0.92) | 1.00x (0.96–1.06) | 0.98x (0.95–1.02) |
| push_front | 0.97x (0.92–1.01) | 1.08x (0.97–1.13) | 1.01x (0.96–1.03) |
| push_back (reserved) | 1.74x (1.66–1.86) | 1.98x (1.87–2.11) | 1.03x (0.98–1.06) |
| push_front (reserved) | 1.62x (1.57–1.71) | 2.05x (1.98–2.15) | 1.09x (1.06–1.12) |
| pop_back | 3.82x (3.55–4.55) | 7.25x (6.23–8.82) | 1.19x (1.14–1.24) |
| pop_front | 1.66x (1.55–1.84) | 3.66x (3.16–4.32) | 1.18x (1.15–1.21) |
| index | 4.09x (3.93–4.22) | 2.62x (2.54–2.81) | 2.49x (2.42–2.58) |
| front | 0.50x (0.49–0.51) | 0.50x (0.49–0.51) | 0.79x (0.79–0.79) |
| back | 1.00x (0.96–1.00) | 1.49x (1.45–1.55) | 1.21x (1.18–1.26) |
| take_back | 1.79x (1.66–2.02) | 2.98x (2.70–3.32) | 1.22x (1.18–1.35) |
| take_front | 2.10x (1.91–2.32) | 3.44x (3.28–3.70) | 1.20x (1.15–1.25) |
| **mixed (avg)** | **1.48x** (1.44–1.52) | **1.73x** (1.66–1.82) | **1.09x** (1.07–1.10) |

- **`reserve` is the push win.** `std::deque` has no `reserve`, so its reserved rows are just its plain pushes again. Without `reserve`, `ring_buffer` copies everything on each doubling, while `std::deque` allocates fixed-size blocks and never moves an element. That makes plain `int32_t` push_back 0.88x.
- **Pops and index are where one array pays off.** A pop is a decrement and a mask. `std::deque` has to check for block boundaries and free empty blocks. Index is one add and mask, against `std::deque`'s lookup into its block map and then into the block.
- **`front` is the one real loss.** For both int types `ring_buffer::front` takes 0.44 ns and `back` takes 0.22 ns (0.28 and 0.27 for strings), and `pop_front` is 2x `pop_back` for `int32_t` too. `std::deque` reads both ends in 0.22 ns. The cause hasn't been looked into yet.
- **`std::string` is close to even.** About 50 ns of each push and 25 ns of each pop is allocating and freeing the 24-character string, which costs the same in both. Index skips that and still gets 2.49x.

Benchmark: [`ring_buffer_benchmark.cpp`](deque/ring_buffer/ring_buffer_benchmark.cpp). Build with `-O2 -DNDEBUG`.
