# P2 vendor source cross-check — non-authoritative corroboration

Status: **PARTIAL — useful source corroboration found; exact target source revision is not established.**

Target binary remains authoritative:
- Factory HAL: `vendor/lib/hw/audio.primary.mt8163.so`
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- Android 9 / ARM32 / MT8163.
- Binary ABI baseline: [ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md).

## 1. MT8163 vendor source corroboration

Public source repository: [488315-archive/mt8163-vendor](https://github.com/488315-archive/mt8163-vendor/tree/2006b5183be2fac6a82eff7d9ed09c2633acafcc), commit `2006b5183be2fac6a82eff7d9ed09c2633acafcc`.

The following files are useful cross-checks, not proof that this exact revision produced the target factory ELF:

| Source | Git blob SHA | Relevant evidence |
|---|---|---|
| [AudioALSAHardware.h](https://github.com/488315-archive/mt8163-vendor/blob/2006b5183be2fac6a82eff7d9ed09c2633acafcc/mediatek/proprietary/hardware/audio/common/V3/include/AudioALSAHardware.h#L151-L160) | `32531dbfffa4eb98c1fa08e428000336160ac200` | Declares the seven xWay methods and `ReadRefFromRing(void*, uint32_t, void*)` |
| [AudioALSAHardware.cpp](https://github.com/488315-archive/mt8163-vendor/blob/2006b5183be2fac6a82eff7d9ed09c2633acafcc/mediatek/proprietary/hardware/audio/common/V3/aud_drv/AudioALSAHardware.cpp#L2887-L2970) | `3b661adfc93b23304cea158564c130ed3c541f4d` | Implements xWay forwarding to `Play2Way` / `Record2Way`; forwards reference audio to `AudioVUnlockDL` |
| [hardware/audio_mtk.h](https://github.com/488315-archive/mt8163-vendor/blob/2006b5183be2fac6a82eff7d9ed09c2633acafcc/mediatek/proprietary/hardware/audio/common/include/hardware/audio_mtk.h#L54-L72) | `7607bcfb6941d512baabe7bdb5e850d273e94cce` | Declares a legacy C extension with device-pointer first arguments |

**ABI warning:** the legacy `audio_hw_device_mtk` declaration does not match the recovered target binary's 16-slot private tail. Its callback ordering and extra voice-unlock entries differ from the target's HIDL 4.1 bridge mapping. Do not use this header to calculate target offsets, populate the private tail, or replace the binary-derived ABI table.

The C++ method declarations and implementations corroborate the internal xWay signatures and forwarding destinations. They do not independently prove that the source revision matches the target binary or that an external caller exists in the target firmware.

## 2. Voice-unlock lifecycle findings in the V3 source

Source: [AudioVUnlockDL.cpp](https://github.com/488315-archive/mt8163-vendor/blob/2006b5183be2fac6a82eff7d9ed09c2633acafcc/mediatek/proprietary/hardware/audio/common/V3/aud_drv/AudioVUnlockDL.cpp), blob SHA `7b3b7dba8f1dac7f92e1a51cd2303b47213a2ec3`.

Observed in this source revision:

1. `startInput()` calls `pthread_create()` and polls `mReadThreadActive`.
2. `stopInput()` sets `mReadThreadExit`, then polls `mReadThreadActive` for at most 50 × 50 ms. It does not call `pthread_join()`.
3. `ReadRoutine` waits at most 50 × 10 ms for `mReadFunctionActive` to clear, then sets `mReadThreadActive = false`. If the bounded wait expires while a reference callback is still active, the worker-active flag can cease to represent callback quiescence.
4. `SetInputStandBy(true)` waits at most 30 × 3 ms for `mReadFunctionActive`, then continues even if the flag remains set.
5. `freeInstance()` deletes the singleton without calling `stopInput()`. The inspected destructor closes the SRC object and frees its temporary buffer; it does not stop or join the worker.
6. The source file contains neither `pthread_detach` nor `pthread_join`.

The last point conflicts with the factory-ELF disassembly: target `ReadRoutine` calls `pthread_self()` and `pthread_detach()`. This is direct evidence that the public V3 source snapshot is not yet proven identical to the target binary revision. Keep the target binary finding and source finding separate until a matching vendor source revision is found.

## 3. Target factory ELF: wrapper call order

Evidence: GitHub Actions run `43564228492`, artifact `mt8163-primary-hal-p2-evidence`, report `p2-voiceunlock-lifecycle-disassembly.txt`. The artifact was generated against the pinned factory HAL SHA-256 above; factory ELF identity verification and the extended lifecycle job both passed.

The target binary's four `AudioALSAHardware` wrappers were disassembled:

| Method | Address | Observed call path |
|---|---:|---|
| `startVoiceUnlockDL()` | `0x000ce80c` | Calls `AudioVUnlockDL::getInstance()`, then tail-branches to `AudioVUnlockDL::startInput()` |
| `stopVoiceUnlockDL()` | `0x000ce848` | Calls `AudioVUnlockDL::getInstance()`, then tail-branches to `AudioVUnlockDL::stopInput()` |
| `freeVoiceUnlockDLInstance()` | `0x000ce884` | Tail-branches directly to `AudioVUnlockDL::freeInstance()`; no stop call is present in this wrapper |
| `getVoiceUnlockDLInstance()` | `0x000ce8bc` | Calls `AudioVUnlockDL::getInstance()`, then returns whether the result is non-null |

The target `AudioVUnlockDL::freeInstance()` body at `0x000b49a0` checks the singleton pointer, destroys the SRC object when present, deletes the dynamic buffer when present, deletes the singleton allocation, and clears the singleton pointer. Its disassembled body contains no call to `stopInput()`, `pthread_join()`, or another worker-shutdown function.

**Target-binary teardown conclusion:** neither the `freeVoiceUnlockDLInstance()` wrapper nor the inspected `freeInstance()` body enforces `stop → worker termination → callback drain → free`. The separate stop wrapper is not automatically invoked by the free wrapper. Therefore, the target binary exposes a teardown path whose safety depends on external call ordering and/or an unobserved synchronization contract. Static evidence does not establish that a real caller actually invokes free while the worker is active; it does establish that these functions do not enforce the required ordering internally.

The stop path also must not be summarized as a simple bounded wait followed by return: the target `stopInput()` disassembly contains a follow-on repeated check/log branch after its initial polling loop. This report does not assign source-level field names to the target's byte flags because the exact matching source revision and field mapping remain unverified.

The wrapper disassembly contains no explicit serialization lock around these operations. That absence in the small wrapper bodies does not exclude synchronization in callers, in `getInstance()`, or in code not included in the targeted disassembly.

## 4. Framework xWay call-chain precedent — not target-build proof

A separate MediaTek framework source tree, [CM-Huawei/Source_MT6582](https://github.com/CM-Huawei/Source_MT6582), commit `01ffc3cca24a197d66b9662822de150d5ab6fbc0`, contains:
- [AudioSystem.cpp](https://github.com/CM-Huawei/Source_MT6582/blob/01ffc3cca24a197d66b9662822de150d5ab6fbc0/frameworks/av/media/libmedia/AudioSystem.cpp#L936-L1018): public xWay methods forward through `IAudioFlinger`.
- [AudioFlinger.cpp](https://github.com/CM-Huawei/Source_MT6582/blob/01ffc3cca24a197d66b9662822de150d5ab6fbc0/frameworks/av/services/audioflinger/AudioFlinger.cpp#L270-L332): xWay methods call the primary HAL extension; most xWay calls use `mHardwareLock`, while the shown `ReadRefFromRing` / voice-unlock calls do not acquire it.
- [AudioPCMxWay.cpp](https://github.com/ShadeWalker/Tango_AL813/blob/b50b1b7491dc9c5e6b92c2d94503635c43e93200/frameworks/av/media/libmedia/AudioPCMxWay.cpp#L161-L333): a client implementation starts/stops xWay and dispatches read/write operations through `AudioSystem`.

This establishes a known MediaTek framework integration pattern and identifies a possible caller class. These repositories are not the exact MT8163 Android 9 framework source and must not be presented as proof that the target head unit ships or uses this path.

## 5. Target MT8163 framework evidence

The public [mt8173-dev/android_device_amazon_sloane](https://github.com/mt8173-dev/android_device_amazon_sloane/tree/7a41e2f9314b0b20f49538718e5e515824c2f97d) device source snapshot includes [patch 0004](https://github.com/mt8173-dev/android_device_amazon_sloane/blob/7a41e2f9314b0b20f49538718e5e515824c2f97d/patches/frameworks/av/0004-mt8163-libmedia-add-missing-mtkaudiostub.patch#L24-L86), which supplies no-op/false `AudioSystem` voice-unlock stubs rather than a real AudioFlinger forwarding path. Repository search in that target-device snapshot did not find `AudioPCMxWay`, `xWayPlay` or `xWayRec` callers.

This is supporting evidence only: a device patch repository is not a complete source checkout of the exact installed framework, and it cannot exclude vendor-private, dynamically loaded or application-level clients.

## 6. Current conclusion and remaining gates

- First eight private callback C++ method signatures and internal forwarding destinations are corroborated by public MT8163 V3 source.
- The legacy C extension header is not layout-compatible with the target binary's 16-slot tail and is explicitly non-authoritative for offsets.
- The target firmware scan covers 1,294 ELF/bytecode/archive candidates; no new xWay client was identified. The scan is symbol/string based and cannot prove absence of stripped or indirect callers.
- The target-device framework snapshot contains no-op voice-unlock stubs and no xWay references, but the exact installed Android 9 framework source/build remains unpinned.
- The V3 voice-unlock lifecycle source has bounded drain waits and a delete-without-stop path; it is not proven to match the target binary. The target binary independently confirms that the free wrapper does not call stop and the free body does not shut down the worker.
- The target `ReadRoutine` calls `pthread_self()` and `pthread_detach()`; the exact matching source revision remains unknown.
- No callback prototype or lifetime contract is promoted to implementation-ready solely on the basis of these cross-repository sources.

P2 remains **OPEN**. Preserve the factory components, keep Primary HAL stream opens fail-closed, and do not implement guessed private callbacks.
