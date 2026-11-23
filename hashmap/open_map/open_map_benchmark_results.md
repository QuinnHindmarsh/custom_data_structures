# open_map benchmark results

- Run: 2026-11-23 20:40 (took 11m 28s)
- Compiler: 14.2.0
- Optimised build: yes
- Asserts: off
- Map size: ~1,000,000 keys. Ops are repeated in passes of 1,000,000 per phase; each time is the median pass.

Phases: **insert** N new keys into an empty map. **update** the same keys again. **find hit / miss** look up present / absent keys. **erase miss** erase absent keys. **erase hit** erase every key. **mixed** N random ops on a map holding N keys: 25% insert (new key), 25% update (live key), 25% find (half hit, half miss), 25% erase (half hit, half miss).

## Summary: best open_map load factor vs std::unordered_map

Speedup = unordered_map time / open_map time. Above 1x means open_map is faster. **Bold** = open_map is slower.

| keys | load factor | ops per phase | insert | update | find hit | find miss | erase miss | erase hit | mixed |
|:--|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| int32_t | 0.75 | 100,000,000 | 3.46x | 1.87x | 2.26x | 2.82x | 2.97x | 12.91x | 3.84x |
| int64_t | 0.75 | 100,000,000 | 3.61x | 1.88x | 2.27x | 2.75x | 3.04x | 12.43x | 3.91x |
| std::string | 0.80 | 50,000,000 | 1.55x | 1.03x | 1.21x | 2.29x | 2.29x | 3.68x | 1.79x |

## Final runs

Time per operation in ns (median pass), lower is better.

### int32_t keys, lf 0.75, 100,000,000 ops per phase (100 passes of 1,000,000)

Correctness check: OK

| phase | open_map (lf 0.75) | unordered_map | speedup |
|:--|--:|--:|--:|
| insert | 72.1 | 249.7 | 3.46x |
| update | 26.5 | 49.6 | 1.87x |
| find hit | 17.2 | 38.9 | 2.26x |
| find miss | 16.4 | 46.2 | 2.82x |
| erase miss | 16.7 | 49.5 | 2.97x |
| erase hit | 18.1 | 233.5 | 12.91x |
| mixed | 40.5 | 155.8 | 3.84x |

### int64_t keys, lf 0.75, 100,000,000 ops per phase (100 passes of 1,000,000)

Correctness check: OK

| phase | open_map (lf 0.75) | unordered_map | speedup |
|:--|--:|--:|--:|
| insert | 63.3 | 228.7 | 3.61x |
| update | 25.9 | 48.6 | 1.88x |
| find hit | 16.8 | 38.1 | 2.27x |
| find miss | 16.1 | 44.3 | 2.75x |
| erase miss | 16.0 | 48.7 | 3.04x |
| erase hit | 17.7 | 219.9 | 12.43x |
| mixed | 38.4 | 150.2 | 3.91x |

### std::string keys, lf 0.80, 50,000,000 ops per phase (50 passes of 1,000,000)

Correctness check: OK

| phase | open_map (lf 0.80) | unordered_map | speedup |
|:--|--:|--:|--:|
| insert | 276.5 | 428.0 | 1.55x |
| update | 161.1 | 165.8 | 1.03x |
| find hit | 220.4 | 266.4 | 1.21x |
| find miss | 69.3 | 158.5 | 2.29x |
| erase miss | 70.1 | 160.3 | 2.29x |
| erase hit | 175.1 | 644.0 | 3.68x |
| mixed | 183.7 | 328.0 | 1.79x |

## Load factor sweep

Time per operation in ns (median pass), lower is better. **Bold** = fastest load factor for that phase. Speedup = unordered_map / fastest load factor.

### int32_t keys, 20,000,000 ops per phase (20 passes of 1,000,000)

| phase | lf 0.70 | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | 66.4 | **64.5** | 74.0 | 83.1 | 101.7 | 240.7 | 3.73x |
| update | 26.2 | **25.7** | 26.0 | 26.2 | 26.4 | 48.3 | 1.88x |
| find hit | 17.0 | **16.7** | 16.9 | 16.8 | 16.8 | 38.0 | 2.28x |
| find miss | 16.4 | **16.1** | 16.4 | 16.1 | 16.2 | 44.5 | 2.77x |
| erase miss | 16.5 | **16.3** | 16.5 | 16.3 | 16.6 | 48.8 | 3.00x |
| erase hit | 18.1 | **17.7** | 17.8 | 17.9 | 17.8 | 229.5 | 12.93x |
| mixed | 39.5 | **39.2** | 40.1 | 39.9 | 40.5 | 151.4 | 3.86x |

Score (geometric mean ns/op over all phases, lower is better): 0.70 = 24.8, 0.75 = 24.3, 0.80 = 25.1, 0.85 = 25.4, 0.90 = 26.3. **Best: 0.75**

### int64_t keys, 20,000,000 ops per phase (20 passes of 1,000,000)

| phase | lf 0.70 | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | **63.9** | 65.6 | 69.3 | 74.7 | 87.6 | 268.8 | 4.20x |
| update | 27.1 | 27.9 | 28.4 | **26.8** | 28.9 | 54.8 | 2.05x |
| find hit | 17.7 | 17.8 | 17.8 | **17.2** | 18.5 | 41.9 | 2.44x |
| find miss | 16.9 | 16.6 | 16.4 | **16.3** | 16.5 | 49.5 | 3.03x |
| erase miss | 17.1 | 16.5 | 17.0 | **16.5** | 16.7 | 55.3 | 3.35x |
| erase hit | 18.8 | 18.5 | 18.6 | **18.2** | 18.8 | 249.1 | 13.70x |
| mixed | 41.2 | 41.1 | **39.5** | 40.6 | 40.9 | 165.1 | 4.18x |

Score (geometric mean ns/op over all phases, lower is better): 0.70 = 25.4, 0.75 = 25.4, 0.80 = 25.6, 0.85 = 25.4, 0.90 = 26.8. **Best: 0.75**

### std::string keys, 5,000,000 ops per phase (5 passes of 1,000,000)

| phase | lf 0.70 | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | 277.7 | 277.1 | **275.2** | 285.1 | 304.3 | 495.9 | 1.80x |
| update | 163.9 | 161.0 | **157.8** | 163.3 | 162.9 | 189.2 | 1.20x |
| find hit | 234.7 | 227.4 | **223.5** | 226.3 | 227.4 | 268.8 | 1.20x |
| find miss | 72.0 | 70.7 | **70.6** | 71.5 | 72.0 | 163.1 | 2.31x |
| erase miss | 73.5 | **69.7** | 73.3 | 70.3 | 73.5 | 166.9 | 2.40x |
| erase hit | 185.3 | 176.1 | **175.0** | 175.4 | 179.3 | 669.9 | 3.83x |
| mixed | **179.7** | 185.0 | 182.4 | 185.7 | 185.7 | 335.1 | 1.87x |

Score (geometric mean ns/op over all phases, lower is better): 0.70 = 152.1, 0.75 = 149.0, 0.80 = 148.7, 0.85 = 150.2, 0.90 = 153.3. **Best: 0.80**

