# Firmware binary analysis — repository findings

## Scope and method

The firmware-library analysis workflow scanned **1,117 native ARM objects** from the retained system/vendor library sets. The analysis artifact includes file identification, SHA-256 inventory, ELF symbol/relocation data and focused string/symbol searches. The findings below are static evidence only; they do not prove which optional module is loaded on the target head unit at runtime.

## Critical architecture finding: factory audio control is spread across framework libraries

The following system libraries contain direct references to both `/dev/audiomux` and `/dev/carstatus`:

| Binary | Build ID | Static evidence |
|---|---|---|
| `system/lib/libcarservices_jni.so` | `bf391e545edd2e482b72cd8a431d1c59` | `ExtAudioMuxer`, `SendCmdAudioMux`, `sendCmdCarStatus`, both device paths |
| `system/lib/libaudiopolicymanagerdefault.so` | `5eceb81c4ef58c2e731f28b06dcfa026` | `setAudioOutputMuteTimeout`, both device paths |
| `system/lib/libaudioflinger.so` | `e81f342a372022c2c5a83319b2f22663` | `setAudioOutputMute`, both device paths |

**Impact on the Audio Engine goal:** replacing only the vendor HAL or a car-services JNI bridge would leave factory control paths inside the framework libraries. The Android AudioFlinger and AudioPolicyManager public contracts should remain compatible, but their vendor-specific hooks and direct device-node access must be mapped and brought under the new engine's single ownership model. The exact call sites, ioctl/write operations and framework callers still need disassembly/call-graph confirmation.

## CD3313 and alternative DSP driver implementations

The extracted vendor module set contains three audio/DSP kernel modules with a common character-device design:

| Module | Build ID | Evidence |
|---|---|---|
| `cd3313.ko` | `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56` | `audiomux_misc_device`, `misc_register`, CD3313 I²C, command/read/write/ioctl handlers |
| `fy7604.ko` | `730f24fbce5e5b316379608e493a76343cad1480` | `audiomux_misc_device`, `g_audiomux`, `misc_register`, FY7604 I²C and DSP commands |
| `ak7604.ko` | `30a1e2b6b07cf6590a2fed7e0057af0053354d9b` | `audiomux_misc_device`, `g_audiomux`, `misc_register`, AK7604 I²C and DSP commands |

All three contain an `audiomux` device name and register a misc device. This is a **potential registration collision** if multiple variants are loaded together. They appear to be alternative board/chip implementations, but the actual module-load policy and board selection have not yet been proven. Do not assume all three are active or load them together experimentally.

The CD3313 module also contains references to `/dev/fmradio`, HFP-active state, ACC state, GPIO36 mute, and its own command/event handlers. These interactions must be preserved only where the board's real wiring and call paths confirm them.

## Car-status module relationship

`carstatus.ko` has a separate platform-driver implementation and a `carstatus` misc-device identity. Its symbols include `update_mute`, `mute_mask`, `global_telmute`, and GPIO-related state such as `gpio44_telmute` and `gpio36_mute`. The module imports `filp_open` and its `update_mute` function contains a relocation to that call alongside the `/dev/audiomux` string.

This proves a kernel-side interaction/reference to the audiomux path, but the exact operation and data flow have not yet been recovered. Treat it as an unresolved cross-driver dependency, not as proof that carstatus sends a specific CD3313 ioctl.

## HFP / Bluetooth call audio is part of the factory audio stack

The primary HAL binary `vendor/lib/hw/audio.primary.mt8163.so` (Build ID `f66eaf9e6ac950259f0c253be618d15a`) contains extensive HFP implementation evidence:

- HFP downlink/uplink task and source/sink threads;
- echo-reference and PCM merge components;
- `/dev/socket/hfp_stream`;
- HFP dump paths under `/data/vendor/audiohal/audio_dump/`;
- assertions and lifecycle checks for HFP buffers and handlers.

The HIDL implementation `android.hardware.audio@4.0-impl-mediatek.so` exposes HFP enabled, sample-rate and volume setters; volume is checked against [0, 1]. The `vendor.mediatek.hardware.audio@4.1.so` interface exposes matching HFP methods.

HFP parameters are also handled by `libaudiocustparam_vendor.so` and `libcustom_nvram.so`, including the NVRAM path `/mnt/vendor/nvdata/APCFG/APRDCL/Audio_HFP_Param` and the `AUDIO_HFP_PARAM_STRUCT` record.

**Impact:** HFP call audio, echo reference, uplink microphone and NVRAM parameter handling are mandatory compatibility areas for a full replacement. They cannot be treated as optional music-only bypass paths.

## FM and board-specific evidence

- `fmradio_drv.ko`, `fy7604.ko`, `ak7604.ko`, `cd3313.ko` and `carstatus.ko` are present in the extracted vendor module tree.
- The CD3313 and alternative DSP modules contain `/dev/fmradio` references.
- The paired FM analysis reports no `fm_cust.cfg` in the supplied extracted archive set. The driver may request a tuner threshold override that is not present in these materials; this remains a static finding and requires target confirmation.
- The supplied archive set does not itself establish the active DT/DTBO GPIO mapping or which DSP module the target loads.

## New items to track

1. Map every framework and service call site that opens `/dev/audiomux` or `/dev/carstatus`.
2. Recover the exact `libaudioflinger.so` and `libaudiopolicymanagerdefault.so` mute operations and their callers.
3. Determine the board/module selection mechanism for CD3313, FY7604 and AK7604.
4. Recover the carstatus-to-audiomux interaction from `carstatus.ko` relocations and function flow.
5. Map the full HFP data path, including echo reference, microphone uplink, BT HFP controls and NVRAM parameters.
6. Confirm the actual target board's GPIO, I²C and module-load state before changing any hardware behavior.

## Evidence and limitations

Source: [full firmware library analysis workflow](https://github.com/14march93/mt8163_audio_engine/actions/runs/43201322049), including the `NATIVE_AUDIO_SCAN.txt`, module symbol/relocation reports and focused driver artifacts.

All findings here are static. No kernel module was loaded, no I²C/GPIO writes were made, and no real-device behavior is claimed.
