# open_map benchmark results

- Run: 2026-11-23 16:43
- Compiler: 14.2.0
- Optimised build: yes
- Asserts: off

## Summary: fastest open_map vs std::unordered_map

Speedup = unordered_map time / time of the fastest open_map load factor. Above 1x means open_map is faster. **Bold** = open_map is slower.

| keys | N | insert | update | find hit | find miss | erase miss | erase hit | mixed |
|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| uint32_t | 1,000,000 | 2.80x | 1.94x | 3.84x | 2.76x | 3.36x | 9.25x | 2.71x |
| uint64_t | 1,000,000 | 2.75x | 1.93x | 3.26x | 2.60x | 3.10x | 10.11x | 2.57x |
| std::string | 1,000,000 | **0.97x** | **0.67x** | 1.95x | 1.73x | 2.11x | 3.71x | 1.35x |

## Details

Time per operation in ns, lower is better. **Bold** = fastest open_map load factor. Speedup = unordered_map / fastest open_map.

Phases: **insert** N new keys into an empty map. **update** the same keys again. **find hit / miss** look up present / absent keys. **erase miss** erase absent keys. **erase hit** erase every key. **mixed** 50% find, 25% insert, 25% erase.

### uint32_t keys, N = 1,000,000

1 map(s) x 1 pass(es), best of 3 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | lf 0.95 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | **125.1** | 130.9 | 164.2 | 206.8 | 351.3 | 350.3 | 2.80x |
| update | 21.9 | **21.6** | 23.2 | 22.3 | 24.5 | 41.9 | 1.94x |
| find hit | 20.6 | 18.8 | 20.5 | **18.8** | 19.0 | 72.2 | 3.84x |
| find miss | 28.0 | **27.2** | 28.2 | 27.3 | 28.5 | 75.0 | 2.76x |
| erase miss | **26.7** | 26.8 | 27.3 | 27.0 | 27.3 | 89.8 | 3.36x |
| erase hit | 34.4 | 33.6 | **32.4** | 33.5 | 37.2 | 299.5 | 9.25x |
| mixed | **80.8** | 93.9 | 82.2 | 81.2 | 84.1 | 219.1 | 2.71x |

### uint64_t keys, N = 1,000,000

1 map(s) x 1 pass(es), best of 3 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | lf 0.95 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | **123.3** | 137.4 | 157.7 | 205.7 | 329.5 | 339.1 | 2.75x |
| update | 21.9 | 21.8 | 25.6 | 22.8 | **21.5** | 41.4 | 1.93x |
| find hit | **18.8** | 19.4 | 18.9 | 19.3 | 19.4 | 61.5 | 3.26x |
| find miss | 27.1 | **27.0** | 29.3 | 27.1 | 27.2 | 70.1 | 2.60x |
| erase miss | 26.9 | 27.2 | 27.6 | **26.8** | 27.0 | 83.0 | 3.10x |
| erase hit | **32.4** | 33.0 | 36.4 | 36.0 | 32.9 | 327.5 | 10.11x |
| mixed | 87.4 | 90.4 | **82.5** | 87.8 | 82.7 | 212.0 | 2.57x |

### std::string keys, N = 1,000,000

1 map(s) x 1 pass(es), best of 3 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | lf 0.95 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|--:|
| insert | **591.6** | 651.8 | 635.8 | 733.7 | 929.7 | 573.4 | 0.97x |
| update | 355.0 | 371.7 | 352.6 | 356.4 | **347.3** | 231.5 | 0.67x |
| find hit | 228.4 | **197.0** | 215.2 | 202.0 | 214.0 | 384.1 | 1.95x |
| find miss | 125.2 | 118.9 | 129.7 | **116.7** | 117.9 | 202.3 | 1.73x |
| erase miss | 113.8 | 117.2 | 131.1 | **113.0** | 130.6 | 238.9 | 2.11x |
| erase hit | 234.6 | 232.1 | 239.1 | 224.4 | **214.4** | 795.8 | 3.71x |
| mixed | 414.2 | 420.5 | **408.9** | 413.9 | 440.2 | 552.7 | 1.35x |

