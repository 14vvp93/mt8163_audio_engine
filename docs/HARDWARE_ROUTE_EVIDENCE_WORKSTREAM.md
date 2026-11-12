# Hardware and route evidence workstream

Date: 2026-11-12
Target: MT8163 / Android 9 / vendor kernel 4.9.117
Status: static evidence map and runtime gate; no hardware commands issued

## 1. Known software route families

| Function | Recovered software path | Remaining proof |
|---|---|---|
| Local media / navigation / notifications | Primary HAL PCM path | Runtime ALSA card/PCM mapping and per-usage policy |
| Bluetooth A2DP | Vendor A2DP PCM policy, PCM16 stereo 44.1 kHz | Runtime endpoint and offload state |
| Bluetooth HFP | SCO/HFP capture and playback lifecycle | Complete call-state and endpoint transition tests |
| FM direct playback | MT6627 I2S configuration plus HAL FM_I2S_Playback | Physical I2S pin wiring and audible route |
| FM capture | AUDIO_SOURCE_FM_TUNER selects dedicated capture handler under guards | Runtime capture endpoint and policy patch behavior |
| AUX / AUX2 / DAB | CD3313 logical input/source commands and source GPIO evidence | Actual board population and analog wiring |
| Reverse / OnStar | Firmware references exist | Runtime route contract and safety behavior |

## 2. CD3313 versus alternative DSPs

The firmware contains cd3313.ko, ak7604.ko, and fy7604.ko. Device-tree evidence lists candidate I2C nodes, and init scripts attempt module loads. This does not prove that all chips are populated or bound on the target board. Multiple modules appear to register the same audiomux misc device and may request overlapping GPIO resources.

Do not load alternative modules experimentally. Do not send speculative ioctl payloads. Establish the exact running owner of /dev/audiomux through read-only runtime evidence before hardware integration.

## 3. Safe evidence collection order

1. Obtain read-only /proc/asound/cards and /proc/asound/pcm output from the target.
2. Record /sys/class/sound and ALSA device metadata without opening playback streams.
3. Collect dmesg entries for I2C probe, module registration, GPIO acquisition and audio devices.
4. Read module metadata and /sys/module state; do not call insmod/rmmod.
5. Inspect read-only device-node metadata and process file descriptors to identify the owner of /dev/audiomux.
6. Correlate actual device-tree nodes and pinctrl state with the exact board revision.
7. Only after the evidence map is complete, design a controlled route test with a factory-image rollback path.

## 4. Route policy constraints

- A route declared in Android policy is not proof that it is wired or active.
- A driver/module included in the image is not proof that it is loaded.
- An I2C node in a supplied DTBO is not proof that the physical chip responds.
- A PCM endpoint label is not a numeric ALSA card/device index.
- FM tuner control, direct I2S playback and FM capture are separate paths.
- CD3313 has stereo analog input paths and four analog output attenuators; this is not four-channel PCM.
- HFP/SCO is a call path, not a music route, and must retain dedicated handling.

## 5. Integration decisions

The PCM backend must accept only verified PCM route types and receive endpoint mapping from a separately validated platform configuration. It must not embed guessed card/device numbers.

The hardware broker remains the only AudioCore-facing command owner. CD3313 transactions stay on a serialized control thread, never inside a PCM callback. Alternative DSP transports remain disabled until board identity and module ownership are established.

## 6. Current evidence classification

- HOST VERIFIED: static parsing, unit tests and repository-side checks.
- ANDROID BUILD VERIFIED: ARMv7 build using the configured Android NDK toolchain.
- DEVICE VERIFIED: read-only/runtime evidence or controlled device tests on the actual MT8163 board.

At this stage the route findings are static evidence, not DEVICE VERIFIED. No live hardware write or target-device compatibility claim is made.