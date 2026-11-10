# MT8163 Audio Engine

Reverse-engineering and development project for an improved audio system on MediaTek MT8163 / Android 9.

## Current approach

Do not replace the stock MTK audio HAL wholesale. Preserve its routing, device control, call paths, microphone handling, and existing ACF/HCF backend while developing an independent PCM DSP engine.

### Planned components

1. **CD3313 control layer** — validated userspace ABI for the existing `/dev/audiomux` driver and its I²C register interface.
2. **PCM DSP core** — independent C++ processing library with format-aware input/output, explicit bypass, controlled latency, and host-side tests.
3. **Android integration adapter** — integration at a verified AudioFlinger/HAL processing point; no global hook until call/SCO bypass and routing are understood.
4. **Control API/UI** — separate controls for EQ, bass/treble, loudness, balance and profiles, with safe state persistence. The CD3313 reverse engineering also confirms a four-argument FL/FR/RL/RR control path, which is the hardware basis for separate L/R balance and Front/Rear fader. A host-side rear-level compensation trim is modeled as front-pair attenuation (the chip cannot provide positive gain); hardware routing and factory levels remain to be verified.

### PCM DSP core

The initial independent PCM16 stereo DSP library is in [pcm-dsp/](pcm-dsp/). It currently includes 10-band parametric EQ, preamp, balance, limiter and bit-exact bypass. Its host CMake build and tests passed in [GitHub Actions](https://github.com/14march93/mt8163_audio_engine/actions/runs/42801853494). This is not yet connected to the Android audio path.

## Reverse-engineering documents

- [CD3313 kernel protocol](docs/CD3313_PROTOCOL_RECONSTRUCTION.md)
- [CD3313 command dispatch map](docs/CD3313_COMMAND_DISPATCH.md)
- [MT8163 primary Audio HAL](docs/MT8163_AUDIO_HAL_ANALYSIS.md)
- [MTK audio command service / ACF path](docs/MTK_AUDIO_COMMAND_SERVICE.md)
- [PCM DSP core](pcm-dsp/README.md)

## Development constraints

- Target platform: MT8163, Android 9, ARM 32-bit.
- Initial PCM target: PCM16 stereo at 44.1/48 kHz, subject to runtime negotiation.
- Keep Bluetooth SCO, voice calls, microphone capture and other non-music paths bypassed until explicitly mapped.
- Do not assume every policy-declared route is active on every head unit.
- Do not guess vendor ioctl structures, ACF/HCF parameter layouts, or NVRAM formats.
- All current verification is offline/static or host-side CI. No head-unit runtime validation is claimed.

## Current status

- CD3313 ARM32 module statically analyzed.
- Initial CD3313 command emulator and CI tests implemented.
- Primary MTK Audio HAL filter manager and ACF/HCF parameter paths mapped.
- ATCI audio command service identified; dependent vendor libraries still needed to reconstruct the full ACF/HCF ABI.
- Initial PCM DSP core implemented and host tests passed; Android integration is not implemented yet.
