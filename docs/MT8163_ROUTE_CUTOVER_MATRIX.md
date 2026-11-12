# MT8163 route evidence and cutover matrix

Date: 2026-11-12  
Target: MT8163 / Android 9 / ARM32  
Status: static firmware evidence; awaiting read-only target diagnostics

| Function | Recovered software path | What remains unproven | Cutover gate |
|---|---|---|---|
| Local media | Normal primary HAL PCM | Runtime ALSA endpoint, exact buffer contract | Match /proc/asound topology; fake backend lifecycle; HAL ABI |
| Navigation | Primary HAL output with source policy | Whether it shares the same PCM endpoint and ducking policy | Preserve usage/priority and independent mute ownership |
| Notifications | Primary HAL output | Mixing/ducking and stream lifetime | Verify AudioPolicy usage and transition behavior |
| A2DP | Bluetooth vendor PCM policy; PCM16 stereo 44.1 kHz policy evidence | Runtime endpoint, offload state, ownership transitions | Verify A2DP PCM/offload and no duplicate primary-HAL ownership |
| HFP/SCO | Dedicated call/SCO input and output path | Runtime SCO PCM endpoints, mic/echo reference and call state | Full call lifecycle; never map to music PCM |
| FM direct | MT6627 tuner control plus HAL FM I2S playback/capture controls | Physical I2S wiring and audible path | Confirm board routing and tuner runtime state |
| FM capture | Dedicated `AUDIO_SOURCE_FM_TUNER` capture handler | Active capture PCM endpoint and patch lifecycle | Verify input route and sample-rate behavior |
| AUX/AUX2/DAB | CD3313 logical input selection and analog input controls | Populated DSP, actual analog wiring, live command owner | Identify /dev/audiomux owner and board chip read-only first |
| Reverse / OnStar | Firmware references and vehicle integration | Exact stream/route and safety contract | Leave unsupported until owner, trigger and safety behavior are proven |

## Critical separation

1. FM tuner control, FM direct I2S playback and FM capture are three different software paths.
2. Bluetooth A2DP is a music transport; HFP/SCO is a bidirectional call transport with microphone and echo-reference handling.
3. CD3313 stereo analog inputs and four analog output attenuators do not imply four-channel PCM.
4. The Normal speaker processing boundary in the supplied HAL is stereo and may fold L/R to mono before MTK filtering when the speaker device bit is set.
5. Presence of `cd3313.ko`, `ak7604.ko`, or `fy7604.ko` in the firmware does not prove which module is loaded or which chip is populated.

## Required target evidence

Use `tools/collect_mt8163_audio_diagnostics.sh` to collect, without opening or mutating hardware:

- `/proc/asound/cards` and `/proc/asound/pcm`;
- `/sys/class/sound` metadata;
- DSP property `ro.hw.dsp.chip`;
- relevant loaded modules;
- device-node metadata for `/dev/audiomux` and `/dev/carstatus`;
- relevant kernel messages and, where permitted, process descriptors holding those nodes.

Do not run insmod/rmmod, do not open `/dev/audiomux` for exploratory ioctls, and do not run playback/capture as a discovery method. After evidence is reviewed, create a separate controlled test plan with factory rollback.

## Validation labels

- `HOST VERIFIED`: source checks and host tests.
- `ANDROID BUILD VERIFIED`: target ABI/toolchain build checks.
- `DEVICE VERIFIED`: only the exact read-only facts or controlled tests actually observed on the physical target.
