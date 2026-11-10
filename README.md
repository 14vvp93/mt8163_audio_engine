# MT8163 Audio Engine

Reverse-engineering and development project for an improved audio system on MediaTek MT8163 / Android 9.

## Current approach

Do not replace the stock MTK audio HAL wholesale. Preserve its routing, device control, call paths, microphone handling, and existing ACF/HCF backend while developing an independent PCM DSP engine.

### Planned components

1. **CD3313 control layer** — reconstructed read/write/ioctl ABI for the existing `/dev/audiomux` driver and its I²C register interface; exact vendor framework transport still needs confirmation.
2. **PCM DSP core** — independent C++ processing library with format-aware input/output, explicit bypass, controlled latency, and host-side tests.
3. **Android integration adapter** — integration at a verified AudioFlinger/HAL processing point; no global hook until call/SCO bypass and routing are understood.
4. **Control API/UI** — separate controls for EQ, bass/treble, loudness, balance and profiles, with safe state persistence. The CD3313 reverse engineering also confirms a four-argument FL/FR/RL/RR control path, which is the hardware basis for separate L/R balance and Front/Rear fader. A host-side rear-level compensation trim is modeled as front-pair attenuation (the chip cannot provide positive gain); hardware routing and factory levels remain to be verified.

### PCM DSP core

The independent C++17 DSP library is in [pcm-dsp/](pcm-dsp/). The original PCM16 stereo engine remains available. A new four-channel Float32 foundation now supports interleaved FL/FR/RL/RR, 32 biquad slots per channel, independent channel gain/EQ, per-channel peak limiting, bit-exact bypass and dual-bank output crossfades for parameter changes. The four-channel core is host-buildable and has dedicated tests; the latest run is not yet verified. Neither core is connected to the Android audio path.

## Reverse-engineering documents

- [CD3313 kernel protocol](docs/CD3313_PROTOCOL_RECONSTRUCTION.md)
- [CD3313 character-device read/write ABI](docs/CD3313_CHAR_DEVICE_ABI.md)
- [CD3313 ioctl and event-queue ABI](docs/CD3313_IOCTL_ABI.md)
- [CD3313 command dispatch map](docs/CD3313_COMMAND_DISPATCH.md)
- [CD3313 register image and datasheet correlation](docs/CD3313_REGISTER_IMAGE.md)
- [CD3313 static driver risk audit](docs/CD3313_DRIVER_RISK_AUDIT.md)
- [CD3313 userspace-client scan](docs/CD3313_USERSPACE_CLIENT_SCAN.md)
- [MT8163 primary Audio HAL](docs/MT8163_AUDIO_HAL_ANALYSIS.md)
- [MTK audio command service / ACF path](docs/MTK_AUDIO_COMMAND_SERVICE.md)
- [HEQ APK static analysis](docs/HEQ_APK_ANALYSIS.md)
- [HEQ decompiled algorithm analysis](docs/HEQ_ALGORITHM_ANALYSIS.md)
- [PCM DSP core](pcm-dsp/README.md)

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
- PCM16 stereo core and four-channel Float32 DSP foundation implemented; latest four-channel CI result and Android integration remain to be verified.
- HEQ APK decompiled and analyzed: it explicitly recognizes CD3313 through a hidden android.audio.AudioEffect ASP API. Its framework/native transport is not included in the APK; the other external-DSP adapters remain chip-specific.
