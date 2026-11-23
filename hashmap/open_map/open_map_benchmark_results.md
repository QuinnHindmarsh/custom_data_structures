# open_map benchmark results

- Run: 2026-11-23 20:19
- Compiler: 14.2.0
- Optimised build: yes
- Asserts: off
- N = 1,000,000 keys per type, mixed = 1,000,000 ops

## Summary: fastest open_map vs std::unordered_map

Speedup = unordered_map time / time of the fastest open_map load factor. Above 1x means open_map is faster. **Bold** = open_map is slower.

| keys | insert | update | find hit | find miss | erase miss | erase hit | mixed |
|:--|--:|--:|--:|--:|--:|--:|--:|
| int32_t | 4.71x | 1.97x | 2.34x | 2.91x | 3.15x | 13.61x | 3.90x |
| int64_t | 4.07x | 1.94x | 2.24x | 2.78x | 2.99x | 12.58x | 3.94x |
| std::string | 1.61x | 1.05x | 1.25x | 2.29x | 2.30x | 3.66x | 1.79x |

## Details

Time per operation in ns, lower is better. **Bold** = fastest open_map load factor. Speedup = unordered_map / fastest open_map.

Phases: **insert** N new keys into an empty map. **update** the same keys again. **find hit / miss** look up present / absent keys. **erase miss** erase absent keys. **erase hit** erase every key. **mixed** N random ops on a map holding N keys: 25% insert (new key), 25% update (live key), 25% find (half hit, half miss), 25% erase (half hit, half miss).

### int32_t keys, N = 1,000,000

Best of 3 runs. Correctness check: OK

| phase | lf 0.70 | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | 70.0 | **64.0** | 72.1 | 76.8 | 110.0 | 301.3 | 4.71x |
| update | **25.8** | 25.9 | 26.7 | 26.2 | 26.7 | 51.0 | 1.97x |
| find hit | 17.6 | 16.9 | 16.8 | 16.9 | **16.7** | 39.1 | 2.34x |
| find miss | 16.1 | **16.0** | 16.1 | 16.0 | 16.0 | 46.6 | 2.91x |
| erase miss | 16.8 | 16.3 | 16.8 | 16.4 | **16.0** | 50.4 | 3.15x |
| erase hit | 17.8 | 18.0 | 18.1 | 17.6 | **17.3** | 235.4 | 13.61x |
| mixed | 39.0 | 38.8 | **38.8** | 41.2 | 41.2 | 151.4 | 3.90x |

### int64_t keys, N = 1,000,000

Best of 3 runs. Correctness check: OK

| phase | lf 0.70 | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | 60.5 | **57.2** | 60.6 | 68.2 | 76.1 | 233.0 | 4.07x |
| update | 26.7 | **24.4** | 24.8 | 25.0 | 24.8 | 47.2 | 1.94x |
| find hit | 17.5 | 16.7 | 16.5 | **16.4** | 16.5 | 36.8 | 2.24x |
| find miss | 16.2 | 15.7 | 15.6 | 15.6 | **15.5** | 43.2 | 2.78x |
| erase miss | 16.8 | 15.9 | 15.9 | 15.9 | **15.8** | 47.2 | 2.99x |
| erase hit | 18.4 | 17.3 | 17.3 | **17.2** | 17.4 | 216.6 | 12.58x |
| mixed | 37.5 | **37.1** | 37.3 | 37.3 | 37.4 | 146.4 | 3.94x |

### std::string keys, N = 1,000,000

Best of 3 runs. Correctness check: OK

| phase | lf 0.70 | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | 263.1 | **259.8** | 263.9 | 272.3 | 288.3 | 419.6 | 1.61x |
| update | 160.7 | 156.6 | **156.2** | 163.5 | 157.1 | 163.6 | 1.05x |
| find hit | **212.5** | 220.5 | 218.2 | 213.9 | 215.6 | 266.2 | 1.25x |
| find miss | 68.3 | **67.6** | 69.0 | 69.0 | 67.8 | 154.6 | 2.29x |
| erase miss | 68.8 | 68.4 | 70.8 | 71.6 | **67.7** | 156.0 | 2.30x |
| erase hit | 175.0 | 170.0 | 174.1 | **168.6** | 169.2 | 617.7 | 3.66x |
| mixed | 177.8 | 186.3 | 178.1 | 182.4 | **177.4** | 318.1 | 1.79x |

