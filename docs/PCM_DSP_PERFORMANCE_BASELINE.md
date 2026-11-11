# PCM DSP performance baseline

## Revision under test

- Repository: `14march93/mt8163_audio_engine`
- Branch: `main`
- Commit: `8772d7dca709a72276f47c18cb03928dd86759a3`
- Measurement date: 2026-11-11
- Build type: CMake Release
- Benchmark: `pcm-dsp/benchmarks/benchmark_main.cpp`

## Host benchmark results

The benchmark exercises the four-channel processing chain with 32 enabled PEQ stages per channel, four independent channel gains and time alignment, Linkwitz-Riley bass management, all 12 multiband compressor instances, and output limiting.

| Sample rate | Frames | Average | p95 | Maximum | Average CPU, one core |
|---:|---:|---:|---:|---:|---:|
| 44,100 Hz | 128 | 93.68 us | 152.69 us | 189.39 us | 3.23% |
| 44,100 Hz | 256 | 189.57 us | 301.39 us | 357.20 us | 3.27% |
| 44,100 Hz | 512 | 347.98 us | 362.13 us | 756.77 us | 3.00% |
| 48,000 Hz | 128 | 90.03 us | 97.10 us | 190.33 us | 3.38% |
| 48,000 Hz | 256 | 174.74 us | 182.13 us | 332.55 us | 3.28% |
| 48,000 Hz | 512 | 345.74 us | 362.07 us | 608.07 us | 3.24% |

Runner: GitHub-hosted Linux x86-64. These numbers characterize the host build only. They are not MT8163 performance measurements and must not be used to claim real-time capability on the head unit.

## Android ARMv7 build verification

The same revision successfully built the static DSP library and JNI shared library with Android NDK r27c. The workflow inspected both the extracted DSP object and JNI library with `readelf`; both reported `ELF32` and `Machine: ARM`.

Artifact: [mt8163-audio-engine-android-armv7](https://github.com/14march93/mt8163_audio_engine/actions/runs/42961735718/artifacts/13828873148)

This proves the output architecture and successful cross-compilation. It does not prove execution on the MT8163 device.

## Device benchmark required before audio integration

Run the offline/test JNI harness on the actual Android 9 / MT8163 head unit before considering an AudioFlinger/HAL integration:

1. Test 44.1 kHz and 48 kHz separately.
2. Test 128, 256 and 512 frame blocks.
3. Warm up for at least 10 seconds before collecting samples.
4. Collect at least 10,000 block timings using `System.nanoTime()` around the native processing call.
5. Report p50, p95, p99 and maximum processing time, plus block deadline and missed-deadline count.
6. Repeat after thermal stabilization and with the normal head-unit workload running.
7. Keep this JNI array-based harness classified as offline validation: array pinning/copying and Java synchronization make it unsuitable as a production audio callback.

For a block of N frames, the nominal processing deadline is `N / sample_rate` seconds. The production integration must measure the actual callback's available budget and scheduling jitter, not just the DSP function's average execution time.

## Integration status

- Host DSP tests: passing on the recorded revision.
- Host performance benchmark: passing; measurements above.
- Android ARMv7 cross-build: passing.
- Android 9 runtime test on MT8163: not yet performed.
- AudioFlinger / MTK HAL integration: not implemented.
- CD3313 physical subwoofer route: not identified; software sub output remains isolated.

Do not enable the DSP globally or replace the stock primary HAL based solely on host benchmark or cross-build results.
