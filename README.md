# MT8163 Audio Engine

Reverse-engineering and development project for an improved audio system on MediaTek MT8163 / Android 9.

## Current approach

Do not replace the stock MTK audio HAL wholesale. Preserve its routing, device control, call paths, microphone handling, and existing ACF/HCF backend while developing an independent PCM DSP engine.

### Planned components

1. **CD3313 control layer** — kernel command semantics, hidden Java API/Binder route, JNI transport, and ioctl framing are recovered. Physical board routing and live-device validation remain separate tasks.
2. **PCM DSP core** — independent C++ processing library with format-aware input/output, explicit bypass, controlled latency, and host-side tests.
3. **Android integration adapter** — integration at a verified AudioFlinger/HAL processing point; no global hook until call/SCO bypass and routing are understood.
4. **Control API/UI** — separate digital DSP controls plus CD3313-supported source, volume, mute, bass/treble/loudness and four-output level controls. The recovered setBalance(FL,FR,RL,RR) API maps to the driver's four analog output attenuators. The chip does not provide independent per-output PEQ or delay.

### PCM DSP core

The independent C++17 DSP library is in [pcm-dsp/](pcm-dsp/). The original PCM16 stereo engine remains available. A new four-channel Float32 foundation now supports interleaved FL/FR/RL/RR, 32 biquad slots per channel, independent channel gain/EQ, per-channel peak limiting, bit-exact bypass and dual-bank output crossfades for parameter changes. The four-channel core is host-buildable and has dedicated tests. Recent host core, stability, and performance workflows pass; the Android test bench and DSP remain disconnected from the Android system audio path.

### Verified signal boundary

The recovered Normal playback path is two-channel. For any output-device
mask containing the speaker bit `0x02`, the HAL folds L/R to mono and
duplicates that sample into the two-channel buffer **before** the MTK/BES
post-processing callback. The recovered callback is in the Normal handler
only; it is not a global AudioFlinger hook.

The CD3313 datasheet describes three selectable stereo analog inputs and four
analog outputs (front-left, front-right, rear-left, rear-right), with four
independent output attenuators. It does not expose a four-channel PCM input or
four independent per-output PEQ banks. Therefore the current evidence supports
a stereo/mono digital DSP plus four analog speaker-level controls, not four
independent digital PCM channels. The four-channel Float32 engine remains a
host/test foundation until a separate multichannel digital path is proven.
Per-speaker PEQ or delay cannot be claimed through CD3313 alone.

## Reverse-engineering documents

- [CD3313 kernel protocol](docs/CD3313_PROTOCOL_RECONSTRUCTION.md)
- [CD3313 character-device read/write ABI](docs/CD3313_CHAR_DEVICE_ABI.md)
- [CD3313 ioctl and event-queue ABI](docs/CD3313_IOCTL_ABI.md)
- [CD3313 command dispatch map](docs/CD3313_COMMAND_DISPATCH.md)
- [CD3313 register image and datasheet correlation](docs/CD3313_REGISTER_IMAGE.md)
- [CD3313 static driver risk audit](docs/CD3313_DRIVER_RISK_AUDIT.md)
- [CD3313 userspace-client scan](docs/CD3313_USERSPACE_CLIENT_SCAN.md)
- [CD3313 hidden AudioEffect API and Binder route](docs/CD3313_HIDDEN_AUDIOEFFECT_API.md)
- [CD3313 API recovery requirements](docs/CD3313_API_RECOVERY_REQUIREMENTS.md)
- [MT8163 primary Audio HAL](docs/MT8163_AUDIO_HAL_ANALYSIS.md)
- [MT8163 FM routing matrix](docs/MT8163_FM_ROUTING_MATRIX.md)
- [MT6627 FM driver ioctl ABI](docs/MT6627_FM_DRIVER_ABI.md)
- [Carservices Java / FM audio flow](docs/CARSERVICES_JAVA_AUDIO_FLOW.md)
- [MT8163 output-device dispatch audit](docs/MT8163_OUTPUT_DEVICE_DISPATCH_AUDIT.md)
- [Normal speaker PCM format and mono-fold audit](docs/MT8163_NORMAL_SPEAKER_PCM_FORMAT.md)
- [MTK process call-site audit](docs/MT8163_MTK_PROCESS_CALLSITE_AUDIT.md)
- [MTK filter-manager lifecycle and route selection](docs/MT8163_MTK_FILTER_MANAGER_LIFECYCLE.md)
- [ATCI ACF/HCF parameter protocol](docs/AUDIOCMD_ATCI_ACF_HCF_PROTOCOL.md)
- [Android audio integration gate](docs/ANDROID_AUDIO_INTEGRATION_GATE.md)
- [MTK audio command service / ACF path](docs/MTK_AUDIO_COMMAND_SERVICE.md)
- [HEQ APK static analysis](docs/HEQ_APK_ANALYSIS.md)
- [HEQ decompiled algorithm analysis](docs/HEQ_ALGORITHM_ANALYSIS.md)
- [PCM DSP core](pcm-dsp/README.md)
- [ADB audio/Device Tree diagnostics collector](tools/README.md)

## Development constraints

- Target platform: MT8163, Android 9, ARM 32-bit.
- DSP foundations: PCM16 stereo compatibility core and four-channel Float32 FL/FR/RL/RR core at 44.1/48 kHz, subject to runtime negotiation.
- Keep Bluetooth SCO, voice calls, microphone capture and other non-music paths bypassed until explicitly mapped.
- Do not assume every policy-declared route is active on every head unit.
- Do not guess vendor ioctl structures, ACF/HCF parameter layouts, or NVRAM formats.
- All current verification is offline/static or host-side CI. No head-unit runtime validation is claimed.

## Current status

- CD3313 ARM32 module statically analyzed.
- CD3313 host-side command/state emulator models deterministic state transitions, the exact 8-slot reg_bufs cache, the 16-page diagnostic view, the read identifier, speaker codes, input selectors, and event-queue requests; no device I/O is performed by the emulator.
- Primary MTK Audio HAL filter manager and ACF/HCF parameter paths mapped.
- ATCI audio command service identified; dependent vendor libraries still needed to reconstruct the full ACF/HCF ABI.
- PCM16 stereo core and four-channel Float32 DSP foundation implemented; recent host core, stability, and performance CI runs pass. The Android test APK is a bring-up/test bench, not a finished end-user control app; persistent user profiles and system-audio integration are not implemented.
- HEQ APK decompiled and analyzed: it explicitly recognizes CD3313 through the hidden android.audio.AudioEffect ASP API. The matching boot-carservices.vdex supplies the Java/Binder layer and command arrays; the native ExtAudioMuxer JNI transport and ioctl framing are now recovered and documented in [CD3313 ioctl ABI](docs/CD3313_IOCTL_ABI.md).
- FM software routing is statically correlated across RadioService, the exact paired CD3313 module, the MT6627 `/dev/fm` driver, the primary Audio HAL, and WMT I2S pin control. The current CD3313 build maps RADIO and DAB to Source 3, AUX and AUX2 to Source 1, with GPIO 474 and 396 as their respective source-pair switches.
- The HAL's exact FM ALSA endpoint branches are resolved: merge mode uses `MRGRX_PLayback` / `MRGRX_CAPTURE`; non-merge mode uses `FM_I2S_Playback` / `FM_I2S_Capture`. The Audio Policy custom extension is confirmed as the source of `FM_DIRECT_CONTROL`: it sets 1 below 48,001 and 0 at/above 48,001 for FM Tuner input patches; the HAL uses true to force 48 kHz.
- The paired MT6627 driver selects class-1 FM metadata: `aud_path=1` (non-merge I2S), I2S enabled, master mode, 32 kHz, and `FM_I2S_PAD_CONN`; the HAL therefore selects `FM_I2S_Playback` / `FM_I2S_Capture`. WMT dynamic FM I2S pinctrl is compiled out, but that does not rule out a fixed SoC pinmux. The I2S sample-rate setter callback is not populated by the MT6627 low-ops table; a null callback returns `-1002`, and the HAL ignores the ioctl result when requesting 48 kHz. Remaining gaps are the active policy XML/property state, numeric ALSA card/PCM indexes, runtime setter behavior, and physical I2S/CD3313 wiring. The supplied extracted firmware set contains no DT/DTBO or boot image; no live device validation or hardware writes are claimed.
