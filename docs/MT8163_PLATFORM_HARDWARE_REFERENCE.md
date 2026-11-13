# MT8163 platform hardware reference

Status: public-source reference correlated with the repository's static firmware analysis. This is not a board-level schematic or a silicon errata document.

Last reviewed: 2026-11-13.

## 1. SoC identity

The target platform is MediaTek MT8163. The exact package suffix fitted to the target head unit (for example, MT8163V/A versus MT8163V/B) has not yet been confirmed from a readable top-mark or trusted boot log. Do not select clock limits or assume a particular bin until that identity is confirmed.

### Publicly documented SoC capabilities

| Block | Published information | Evidence / qualification |
|---|---|---|
| CPU | 4 × Arm Cortex-A53, 64-bit | MediaTek product page |
| Memory controller | DDR3 / LPDDR3, single channel, up to 800 MHz | MediaTek product page |
| GPU | Arm Mali-T720 | MediaTek product page; MP configuration is not specified there |
| Display | Up to 1920 × 1080 listed by MediaTek | SoC capability, not the target panel resolution |
| Video encode | H.264 | MediaTek product page |
| Video decode | H.264, H.265/HEVC, MPEG-1/2/4 | MediaTek product page |
| Camera ISP | Up to 13 MP | MediaTek product page |
| Connectivity | Wi-Fi 4 (802.11 a/b/g/n), Bluetooth, FM, GPS | Integrated/platform capability list; actual RF devices depend on board design |
| Wi-Fi bands | 2.4 GHz and 5 GHz | MediaTek product page |

MediaTek source: https://www.mediatek.com/products/laptops-and-tablets/tablets/mt8163

The MediaTek page is specifically headed MT8163V/A. Third-party references commonly distinguish V/A (up to 1.5 GHz CPU) and V/B (up to 1.3 GHz CPU), and describe a Mali-T720 MP2 configuration and 28 nm process. Treat those details as secondary-source clues only until the target SoC top-mark or its platform configuration confirms the exact variant:
- https://www.notebookcheck.net/Mediatek-MT8163-V-A-1-5-GHz-SoC.169842.0.html
- https://www.notebookcheck.net/Mediatek-MT8163-V-B-1-3-GHz-SoC-Benchmarks-and-Specs.169841.0.html

## 2. Target software/platform facts from this project

These are target-firmware findings, not generic MT8163 guarantees:

- Product target: MT8163 head unit, Android 9, 32-bit ARM userspace.
- The recovered CD3313 kernel module is ELF32 ARM EABI5 and identifies Linux 4.9.117+ as its kernel baseline.
- The factory primary audio implementation is a MediaTek vendor HAL, identified in the firmware as `audio.primary.mt8163.so`.
- The audio stack uses the Linux ALSA/ASoC layer and MediaTek Audio Front End (AFE) / PCM handlers. Exact active PCM card/device indexes are runtime-derived and must not be hard-coded.
- The vendor HAL contains MTK playback handlers, an MTK filter-manager layer and ACF/HCF parameter paths. The complete replacement must preserve required kernel/ALSA and Android framework contracts while taking ownership of factory vendor audio behavior.
- The normal speaker path recovered from the supplied HAL is two-channel PCM. When the speaker bit is present, the HAL folds L/R to mono and duplicates the sample into the two-channel buffer before the MTK/BES post-processing callback. This is a firmware-specific behavior, not an MT8163 silicon limitation.
- The recovered vendor policy declares primary output PCM16 stereo at 44.1 kHz and 48 kHz. This is the policy profile found in this firmware, not a claim that every hardware route supports every rate.
- FM uses separate HAL/AudioPatch and I2S paths in the analyzed firmware. It must not be assumed to pass through the normal music PCM buffer.
- Bluetooth A2DP policy in this firmware exposes PCM16 stereo at 44.1 kHz. SCO/voice paths require separate handling and must remain outside the music DSP path unless explicitly validated.

Related project evidence:
- [Primary Audio HAL analysis](MT8163_AUDIO_HAL_ANALYSIS.md)
- [Audio HAL reconstruction](MT8163_AUDIO_HAL_RECONSTRUCTION.md)
- [Normal speaker PCM format and mono-fold audit](MT8163_NORMAL_SPEAKER_PCM_FORMAT.md)
- [Audio source coverage](MT8163_AUDIO_SOURCE_COVERAGE.md)
- [FM routing matrix](MT8163_FM_ROUTING_MATRIX.md)
- [CD3313 protocol reconstruction](CD3313_PROTOCOL_RECONSTRUCTION.md)

## 3. What the public SoC specification does not tell us

The public MT8163 product brief is not a complete hardware programming manual. It does not provide enough information to implement or safely modify:

- AFE register addresses, bit fields, clock trees or reset sequencing;
- I2S/TDM pinmux, pad drive, timing, master/slave configuration or board wiring;
- DMA descriptor layout, SRAM/DRAM buffer constraints, interrupt behavior or PCM ring details;
- analog codec / external audio processor connections;
- amplifier enable, mute GPIO polarity, power rails or pop-noise sequencing;
- exact active ALSA card/device topology on this head unit.

Do not derive any of these from the CPU/GPU specification. Use the exact target kernel source, Device Tree, boot logs, ALSA enumeration and board-level measurements. No register writes should be attempted from a guessed map.

## 4. Audio-engine implementation implications

1. Treat the SoC AFE/ASoC drivers as the hardware PCM transport layer; keep DSP sample processing in the Audio Engine's format-aware PCM layer.
2. Negotiate the actual stream format, rate, channel count and period/buffer constraints at runtime.
3. Keep music, navigation, FM, A2DP, SCO/HFP, microphone capture and voice-call paths distinct until their actual routes are proven.
4. Preserve the factory route and startup sequencing as read-only reference while implementing one authoritative owner for each route, mute, volume and hardware state.
5. Make hardware-specific behavior a board/platform profile, not a generic MT8163 assumption.
6. Separate host-test evidence, static firmware evidence, exact-target build evidence and live-head-unit validation in all status reports.

## 5. Open evidence items

| Item | Current status | Required evidence |
|---|---|---|
| Exact MT8163 suffix / speed bin | Unconfirmed | Clear SoC top-mark or trusted platform identification |
| GPU MP configuration | Not confirmed for target | Device configuration / kernel GPU driver data |
| Exact kernel revision and config | Partially known from module ABI only | Target `uname -a`, `/proc/version`, `/proc/config.gz` or matching source |
| AFE topology and active PCM devices | Static route analysis only | `/proc/asound/cards`, `/proc/asound/pcm`, ALSA runtime diagnostics |
| I2S pin routing and clocks | Not confirmed | Matching DTS/pinctrl and board measurement |
| External audio processor wiring | Not confirmed | Board schematic, trace inspection and safe bus observation |

## 6. Sources

1. MediaTek, MT8163V/A product brief: https://www.mediatek.com/products/laptops-and-tablets/tablets/mt8163
2. Notebookcheck, MT8163V/A secondary specifications: https://www.notebookcheck.net/Mediatek-MT8163-V-A-1-5-GHz-SoC.169842.0.html
3. Notebookcheck, MT8163V/B secondary specifications: https://www.notebookcheck.net/Mediatek-MT8163-V-B-1-3-GHz-SoC-Benchmarks-and-Specs.169841.0.html
4. Public MT8163 kernel source reference (Amazon platform, not the target head-unit board): https://github.com/mt8163-dev/android_kernel_amazon_mt8163

The Amazon kernel tree is useful for locating MT8163 ASoC/AFE implementation patterns. It is not proof that its board configuration, pinmux, PCM topology or kernel revision matches this head unit.
