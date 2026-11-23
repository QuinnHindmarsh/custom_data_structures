# open_map benchmark results

- Run: 2026-11-23 14:13
- Compiler: 16.1.0
- Optimised build: yes
- Asserts: off

## Summary: fastest open_map vs std::unordered_map

Speedup = unordered_map time / time of the fastest open_map load factor. Above 1x means open_map is faster. **Bold** = open_map is slower.

| keys | N | insert | update | find hit | find miss | erase miss | erase hit | mixed |
|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| char | 16 | 1.18x | 1.72x | 1.94x | 2.17x | 1.52x | 5.84x | 2.26x |
| char | 64 | 1.17x | 1.51x | 1.74x | 2.23x | 1.40x | 7.48x | 2.40x |
| char | 128 | 1.38x | 1.60x | 1.72x | 2.11x | 1.05x | 8.28x | 2.56x |
| uint32_t | 1,000 | 1.41x | 1.57x | 1.77x | 2.19x | 1.48x | 7.80x | 2.60x |
| uint32_t | 100,000 | 1.41x | 1.07x | 1.87x | 1.15x | 1.47x | 5.46x | 1.56x |
| uint32_t | 1,000,000 | 3.50x | 2.00x | 2.50x | 1.45x | 2.70x | 9.96x | 2.55x |
| uint64_t | 1,000 | 2.43x | 1.61x | 1.87x | 2.29x | 2.40x | 8.08x | 2.60x |
| uint64_t | 100,000 | 1.38x | 1.09x | 1.88x | 1.20x | 1.51x | 5.14x | 1.58x |
| uint64_t | 1,000,000 | 3.49x | 1.92x | 2.60x | 1.58x | 2.90x | 9.72x | 2.56x |
| std::string | 1,000 | **0.64x** | **0.36x** | 1.15x | 1.17x | 1.15x | 2.49x | 1.03x |
| std::string | 100,000 | **0.73x** | **0.46x** | 1.16x | **0.89x** | **0.85x** | 2.64x | 1.06x |
| std::string | 1,000,000 | 1.02x | **0.66x** | 1.54x | 1.65x | 1.63x | 3.48x | 1.23x |

## Details

Time per operation in ns, lower is better. **Bold** = fastest open_map load factor. Speedup = unordered_map / fastest open_map.

Phases: **insert** N new keys into an empty map. **update** the same keys again. **find hit / miss** look up present / absent keys. **erase miss** erase absent keys. **erase hit** erase every key. **mixed** 50% find, 25% insert, 25% erase.

### char keys, N = 16

64 map(s) x 976 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | 25.4 | **25.2** | 26.5 | 27.1 | 29.8 | 1.18x |
| update | 1.5 | 1.5 | **1.5** | 1.5 | 2.6 | 1.72x |
| find hit | **1.3** | 1.3 | 1.3 | 1.3 | 2.5 | 1.94x |
| find miss | 1.2 | 1.2 | 1.2 | **1.1** | 2.5 | 2.17x |
| erase miss | 1.8 | **1.8** | 1.8 | 1.8 | 2.7 | 1.52x |
| erase hit | 2.7 | 2.7 | **2.7** | 2.8 | 15.8 | 5.84x |
| mixed | 2.9 | 3.0 | 2.9 | **2.9** | 6.6 | 2.26x |

### char keys, N = 64

64 map(s) x 244 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **23.7** | 28.9 | 30.5 | 33.2 | 27.8 | 1.17x |
| update | 1.4 | **1.4** | 1.4 | 1.4 | 2.1 | 1.51x |
| find hit | 1.1 | **1.1** | 1.1 | 1.1 | 1.9 | 1.74x |
| find miss | 1.0 | 1.0 | 1.0 | **1.0** | 2.2 | 2.23x |
| erase miss | 1.7 | **1.7** | 1.7 | 1.7 | 2.4 | 1.40x |
| erase hit | 2.0 | **2.0** | 2.0 | 2.0 | 15.0 | 7.48x |
| mixed | 2.7 | **2.7** | 2.7 | 2.7 | 6.5 | 2.40x |

### char keys, N = 128

64 map(s) x 122 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **20.4** | 22.2 | 24.7 | 31.6 | 28.2 | 1.38x |
| update | **1.3** | 1.3 | 1.3 | 1.3 | 2.1 | 1.60x |
| find hit | 1.1 | 1.1 | 1.1 | **1.1** | 1.8 | 1.72x |
| find miss | 0.8 | 0.8 | **0.8** | 0.8 | 1.8 | 2.11x |
| erase miss | 1.7 | 1.7 | **1.7** | 1.7 | 1.8 | 1.05x |
| erase hit | 1.9 | 1.9 | **1.9** | 1.9 | 15.4 | 8.28x |
| mixed | 2.1 | 2.2 | 2.1 | **2.1** | 5.4 | 2.56x |

### uint32_t keys, N = 1,000

16 map(s) x 62 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **20.0** | 22.6 | 25.4 | 31.8 | 28.2 | 1.41x |
| update | **1.8** | 1.9 | 1.9 | 1.9 | 2.8 | 1.57x |
| find hit | **1.4** | 1.4 | 1.4 | 1.5 | 2.4 | 1.77x |
| find miss | **1.4** | 1.5 | 1.5 | 1.5 | 3.1 | 2.19x |
| erase miss | **2.3** | 2.3 | 2.3 | 2.5 | 3.4 | 1.48x |
| erase hit | **2.2** | 2.3 | 2.3 | 2.3 | 17.2 | 7.80x |
| mixed | 3.4 | 3.3 | 3.4 | **3.3** | 8.5 | 2.60x |

### uint32_t keys, N = 100,000

1 map(s) x 10 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | 60.1 | **46.0** | 49.4 | 55.6 | 64.7 | 1.41x |
| update | **8.0** | 14.5 | 14.4 | 14.8 | 8.6 | 1.07x |
| find hit | **4.7** | 12.2 | 12.4 | 12.3 | 8.7 | 1.87x |
| find miss | **9.1** | 13.4 | 13.4 | 13.5 | 10.5 | 1.15x |
| erase miss | **8.3** | 13.9 | 13.8 | 14.1 | 12.2 | 1.47x |
| erase hit | **8.2** | 13.9 | 13.8 | 13.8 | 44.8 | 5.46x |
| mixed | **25.0** | 34.0 | 34.5 | 34.5 | 38.9 | 1.56x |

### uint32_t keys, N = 1,000,000

1 map(s) x 1 pass(es), best of 3 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **77.2** | 85.9 | 91.9 | 106.3 | 270.4 | 3.50x |
| update | 17.5 | 17.5 | 17.4 | **17.2** | 34.4 | 2.00x |
| find hit | **14.1** | 14.4 | 14.3 | 14.8 | 35.1 | 2.50x |
| find miss | 19.8 | **18.8** | 18.8 | 19.0 | 27.3 | 1.45x |
| erase miss | 20.9 | 20.6 | **20.4** | 21.2 | 55.0 | 2.70x |
| erase hit | 24.4 | 25.0 | 25.0 | **24.3** | 242.0 | 9.96x |
| mixed | 62.4 | **60.0** | 64.5 | 61.1 | 152.9 | 2.55x |

### uint64_t keys, N = 1,000

16 map(s) x 62 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **11.4** | 13.0 | 16.4 | 21.3 | 27.7 | 2.43x |
| update | **1.8** | 1.9 | 1.9 | 1.9 | 2.9 | 1.61x |
| find hit | **1.4** | 1.5 | 1.5 | 1.5 | 2.7 | 1.87x |
| find miss | **1.5** | 1.5 | 1.5 | 1.6 | 3.5 | 2.29x |
| erase miss | 2.5 | **1.5** | 1.5 | 1.5 | 3.6 | 2.40x |
| erase hit | 2.3 | **2.3** | 2.3 | 2.4 | 18.2 | 8.08x |
| mixed | 3.5 | **3.3** | 3.4 | 3.3 | 8.7 | 2.60x |

### uint64_t keys, N = 100,000

1 map(s) x 10 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | 62.2 | **45.6** | 49.8 | 57.7 | 62.8 | 1.38x |
| update | **8.0** | 14.7 | 14.8 | 14.9 | 8.7 | 1.09x |
| find hit | **5.1** | 12.4 | 12.3 | 12.5 | 9.6 | 1.88x |
| find miss | **9.8** | 13.4 | 13.3 | 13.1 | 11.7 | 1.20x |
| erase miss | **8.7** | 13.3 | 13.3 | 13.4 | 13.2 | 1.51x |
| erase hit | **8.7** | 13.8 | 13.8 | 13.9 | 44.9 | 5.14x |
| mixed | **26.4** | 39.1 | 39.6 | 39.7 | 41.8 | 1.58x |

### uint64_t keys, N = 1,000,000

1 map(s) x 1 pass(es), best of 3 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **75.8** | 83.3 | 89.5 | 108.8 | 264.7 | 3.49x |
| update | 18.7 | **17.6** | 17.8 | 17.7 | 33.9 | 1.92x |
| find hit | 14.6 | 14.6 | **14.0** | 14.4 | 36.5 | 2.60x |
| find miss | 18.9 | 18.7 | 19.4 | **18.7** | 29.5 | 1.58x |
| erase miss | 21.3 | **18.7** | 18.8 | 18.8 | 54.4 | 2.90x |
| erase hit | 24.9 | 25.0 | 24.7 | **24.6** | 238.6 | 9.72x |
| mixed | 64.1 | 63.3 | **62.8** | 65.7 | 160.8 | 2.56x |

### std::string keys, N = 1,000

16 map(s) x 62 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **101.4** | 106.7 | 112.2 | 122.4 | 64.5 | 0.64x |
| update | 52.0 | 52.1 | 52.8 | **51.6** | 18.8 | 0.36x |
| find hit | **16.7** | 16.8 | 17.0 | 17.2 | 19.1 | 1.15x |
| find miss | **10.3** | 10.5 | 10.8 | 11.0 | 12.1 | 1.17x |
| erase miss | 11.1 | **11.1** | 11.4 | 11.4 | 12.8 | 1.15x |
| erase hit | **19.1** | 19.3 | 19.2 | 19.5 | 47.6 | 2.49x |
| mixed | **30.0** | 30.7 | 30.2 | 30.6 | 30.8 | 1.03x |

### std::string keys, N = 100,000

1 map(s) x 10 pass(es), best of 5 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | 312.5 | **219.3** | 234.6 | 252.2 | 159.6 | 0.73x |
| update | 198.9 | 190.3 | 184.3 | **181.6** | 83.8 | 0.46x |
| find hit | 106.2 | 112.1 | **103.2** | 105.8 | 119.6 | 1.16x |
| find miss | **60.5** | 79.3 | 82.8 | 80.2 | 53.5 | 0.89x |
| erase miss | **60.9** | 82.4 | 82.2 | 85.0 | 51.8 | 0.85x |
| erase hit | 124.8 | 121.5 | 119.5 | **118.4** | 312.6 | 2.64x |
| mixed | **210.7** | 270.4 | 261.9 | 266.4 | 223.9 | 1.06x |

### std::string keys, N = 1,000,000

1 map(s) x 1 pass(es), best of 3 runs. Checksum: OK

| phase | lf 0.75 | lf 0.80 | lf 0.85 | lf 0.90 | unordered_map | speedup |
|:--|--:|--:|--:|--:|--:|--:|
| insert | **419.4** | 448.8 | 456.5 | 513.6 | 428.5 | 1.02x |
| update | **267.5** | 274.1 | 267.6 | 267.8 | 175.2 | 0.66x |
| find hit | **162.8** | 167.5 | 168.5 | 163.0 | 250.8 | 1.54x |
| find miss | 90.8 | 93.2 | 89.6 | **88.7** | 146.8 | 1.65x |
| erase miss | **97.7** | 100.8 | 98.5 | 98.4 | 159.4 | 1.63x |
| erase hit | **181.4** | 185.8 | 185.9 | 184.5 | 632.0 | 3.48x |
| mixed | 315.4 | 313.2 | 309.6 | **309.5** | 381.3 | 1.23x |

