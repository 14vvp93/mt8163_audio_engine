# HIDL syncCallback RefBase promotion audit — 2026-11-14

Status: **PARTIAL LIFETIME PROOF — pre-promotion raw-cookie race and factory drain remain OPEN.**

## Pinned evidence

- HIDL bridge: `vendor/lib/hw/android.hardware.audio@4.0-impl-mediatek.so`
- SHA-256: `58a1a464ae4cdce9e55693795cca2a01f4a50ffcf27a801c226891a27ecbabfc`
- Factory HAL: `vendor/lib/hw/audio.primary.mt8163.so`
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- CI run: [43630639009](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43630639009)
- Evidence artifact: [14128420659](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43630639009/artifacts/14128420659)
- Artifact ZIP SHA-256: `9d55798856978f8bd2b5346c44ec46d0fac32c9dd817b9ded4cf2af7502cc224`
- Audited feature commit: `7456e83b0ac73c487dc933cb281f5773480588a8`
- CI: P2 contract gate **PASS**, reproducible factory/client audit **PASS**, aggregate offline evidence gate **PASS**.

The helper auditor verifies the Thumb-to-ARM veneer opcode sequence, resolves the signed PC-relative target, identifies its executable ELF section, then disassembles the corresponding PLT entry. No function boundary is inferred from a fixed-size instruction window.

## Resolved helper targets

| Direct BL target in `Device::syncCallback` | Resolved PLT symbol | Meaning at call site |
|---:|---|---|
| `0x000284dc` | `android::RefBase::createWeak(void const*) const` at `0x0001963c` | Create weak-reference handle from the Device RefBase subobject |
| `0x000284ec` | `android::RefBase::weakref_type::attemptIncStrong(void const*)` at `0x00019648` | Attempt weak-to-strong promotion |
| `0x000281ec` | `android::RefBase::decStrong(void const*) const` at `0x00019408` | Release promoted Device strong reference |
| `0x0002851c` | `android::RefBase::weakref_type::decWeak(void const*)` at `0x0001966c` | Release temporary weak reference |
| `0x0002817c` | `__android_log_print` at `0x000193b4` | Error/event logging |
| `0x000280ec` | `operator new(unsigned int)` at `0x00019348` | Allocate parameter buffer |
| `0x000281cc` | `__aeabi_memclr8` at `0x000193f0` | Initialize temporary storage |
| `0x000284fc` | `__memcpy_chk` at `0x00019654` | Bounded parameter copy |
| `0x0002850c` | `android::hardware::details::return_status::~return_status()` at `0x00019660` | Destroy callback result wrapper |
| `0x0002820c` | `operator delete(void*)` at `0x00019420` | Free temporary parameter buffer |
| `0x0002811c` | `__stack_chk_fail` at `0x0001936c` | Stack-canary failure path |

These are PLT symbol identities from the pinned ELF's disassembly. The PLT entries are not the implementations of the imported functions; this report establishes the exact calls made by the bridge, not the internals of the external libraries.

## Device cookie promotion dataflow

`Device::syncCallback` is at `0x0001cb4c`, size 240 bytes.

1. Entry captures the event in `r5`, parameter payload pointer in `r4`, and opaque `Device*` cookie in `r2`. The cookie is saved at `sp+4`.
2. At `0x0001cb64–0x0001cb6e`, the bridge loads the cookie's vtable, reads the offset-to-top at `vtable-12`, computes the `RefBase` base-subobject pointer, and calls the `createWeak` PLT veneer `0x000284dc`. The weak-reference handle is saved at `sp+8`.
3. If the cookie is non-null, at `0x0001cb7e–0x0001cb80` the bridge calls `weakref_type::attemptIncStrong` through veneer `0x000284ec`, passing the saved weak-reference handle and a stack identity pointer.
4. The return value is checked at `0x0001cb84`. Only on successful promotion does the bridge copy the original cookie into the strong-object slot at `sp+0`. On promotion failure that slot remains null.
5. The callback member at `Device+0x24` is loaded only after the strong-object slot is checked at `0x0001cb92–0x0001cb96`. Event zero invokes the callback interface through vtable byte offset `0x34` at `0x0001cbe6`. Nonzero events take the logging path; null cookie, failed promotion, or null callback member skip invocation.
6. Normal cleanup converges at `0x0001cbf4–0x0001cc10`. If promotion succeeded, the bridge recomputes the `RefBase` base pointer and calls `decStrong` through `0x000281ec`. If a weak handle was created, it calls `decWeak` through `0x0002851c`.

### Proven conclusion

**Once `syncCallback` successfully promotes the cookie, the Device object is held by a strong RefBase reference while the callback member is accessed and the HIDL callback is invoked.** The bridge releases the promoted strong reference and temporary weak reference on the normal cleanup paths. If promotion fails, it does not dereference `Device+0x24` or invoke the HIDL callback.

This is stronger evidence than merely observing the callback interface's own strong reference at `Device+0x24`: the bridge separately attempts to pin the Device object represented by the raw cookie.

## RefBase implementation behavior and Device-specific limit

The immutable dataset contains both candidate Android 9 libraries:

| Candidate | SHA-256 |
|---|---|
| `system/lib/libutils.so` | `22137df4f6b2e26d30c4a0fab3987adfffa1210266144295d3ab3cccbc5a31f2` |
| `system/lib/vndk-sp-28/libutils.so` | `4e35093a70c58b863e9b14c79a1979caa8b7bf2a05d1630488231ab41df718d0` |

Both export the four RefBase methods above, plus `RefBase::RefBase()`, `RefBase::~RefBase()`, and `RefBase::extendObjectLifetime(int)`. Their relevant implementations are instruction-equivalent:

- `RefBase::RefBase()` initializes the weak-reference control block with `INITIAL_STRONG_VALUE = 0x10000000`, weak count zero, base pointer `this`, and flags at offset `+12` set to zero.
- `extendObjectLifetime(int)` atomically ORs its argument into that flags field.
- `weakref_type::attemptIncStrong` checks flags bit 0 when the strong count is below one. If bit 0 is clear, it drops its temporary weak reference and returns false. If bit 0 is set, it calls the object's `onIncStrongAttempted` virtual hook and may promote.
- The pinned HIDL bridge has no dynamic symbol reference to `RefBase::extendObjectLifetime`.

**Follow-up audit:** CI run [43632653555](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43632653555) passed and inspected both system and VNDK candidates of `libhidlbase.so` and `android.hardware.audio@4.0.so`, in addition to both `libutils.so` candidates. Neither HIDL library candidate has a `RefBase::extendObjectLifetime` symbol reference, and no sized `IBase::IBase` or `IDevice::IDevice` constructor symbols were found in those libraries. The HIDL bridge also has no such reference.

**Concrete Device constructor path — newly resolved:** the second sized constructor body, `Device::Device(audio_hw_device_mtk*)` at `0x0001b8d8` (68 bytes), executes `add.w r0, r5, #44` at `0x0001b8de`, then `BL 0x000281dc` at `0x0001b8e2`. The Thumb-to-ARM veneer at `0x000281dc` resolves to `.plt:0x000193fc`, whose exact entry is `android::RefBase::RefBase()@plt`. Therefore this constructor initializes the RefBase base subobject at `Device + 0x2c`; the imported Android 9 RefBase constructor initializes that subobject's weak-reference state with default flags zero. The first constructor body at `0x0001b87c` (92 bytes) does not contain this call.

**ABI variant identification:** raw, non-demangled ELF symbols identify `0x0001b87d` as `DeviceC2` (base-object constructor, 92 bytes) and `0x0001b8d9` as `DeviceC1` (complete-object constructor, 68 bytes). The C2 body does not initialize the virtual RefBase base; the C1 body performs the `this+0x2c` RefBase constructor call described above. This matches the Itanium ABI distinction between base-object and complete-object construction.

**Remaining limit:** establish the actual object-creation caller and whether the live HIDL cookie is a standalone `DeviceC1` object or a derived `PrimaryDevice`/`MTKPrimaryDevice` object whose most-derived constructor owns virtual-base initialization. The latest audit now decodes the Thumb constructor bodies individually and checks the derived constructors for Device/RefBase callsites. Until that creation path is established, the RefBase initialization path is proven for `DeviceC1`, not for every possible derived runtime object. No `extendObjectLifetime` call was found in the inspected bridge/HIDL libraries.

Additional pinned candidates from run 43632653555:

| Candidate | SHA-256 |
|---|---|
| `system/lib/libhidlbase.so` | `4cbbb56c84d7390c5ad2fdc9d2fff10057b7c0ccdaa2eb412af745a8fbdd7467` |
| `system/lib/vndk-sp-28/libhidlbase.so` | `fe3168c6b47f5bb89648cee201c6c069e24b909b2a5e6935c3b015b3492caec8` |
| `system/lib/android.hardware.audio@4.0.so` | `3074f2be7e737d4534bed19a7edf91612f23c65d6877fc9c75af609fbeaf03a2` |
| `system/lib/vndk-28/android.hardware.audio@4.0.so` | `2e4833a3ab5cbfba58fe4cff6f069e87c62d3be0b07415e5c68ddb21e30f6748` |

## Remaining race and teardown boundary

Factory dispatch at `AudioALSAHardware::setParametersByCallback` (`0x000cf108`) makes the pre-promotion window concrete:

- `0x000cf1f8–0x000cf204` loads the callback pointer from effective GOT cell `0x00192b80` into `r3`.
- `0x000cf208–0x000cf210` separately loads the cookie from effective GOT cell `0x00192b7c` into `r2`.
- `0x000cf218` invokes `BLX r3` with event 0 and the parameter payload.
- There is no visible lock, atomic pair load, owner pin, or in-flight accounting around these loads and invocation in the audited body.
- The whole-ELF direct `BL/BLX` inventory found **no direct callsite** to `setParametersByCallback`. The `AudioALSAHardware::setParameters` body was also disassembled; it has no direct call to this dispatcher. The pinned factory contains 1,575 register-indirect `BLX` sites, and those without proven dataflow remain unattributed.
- The external firmware scan found no confirmed external caller of this method. It cannot exclude stripped or runtime-loaded callers, function-pointer dispatch, or code outside the pinned dataset.

Because the dispatch caller is unresolved, it is not safe to assume that every callback is issued only synchronously inside a HIDL `Device::setParameters` Binder transaction that independently holds a strong `sp<Device>`. That possible serialization must be established from the actual caller path, not inferred from the method name.
- Registration stores callback before cookie; factory destruction clears cookie before callback. Concurrent replacement can therefore expose a mixed pair, and a dispatch that has already loaded the old pair can continue after the global cells are cleared.

The promotion does **not** close the interval before `createWeak`/ `attemptIncStrong` succeeds:

- Factory `AudioALSAHardware::setParametersByCallback` loads the global callback pointer and cookie from separate GOT cells, then calls the function pointer. No lock, atomic pair load, owner pin, or in-flight counter is visible around those operations in the audited body.
- Factory `setupParametersCallback` stores callback and cookie separately, without a visible synchronization primitive. Concurrent replacement can expose a mixed callback/cookie pair unless an external serialization contract exists.
- Factory destruction clears the cookie cell `0x00192b7c` and then callback cell `0x00192b80`, but does not wait for a dispatch that already loaded the old pair.
- The HIDL `Device` destructor calls legacy HAL close before releasing its callback strong-reference members at `+0x24` and `+0x28`. The factory close/destructor path has no proven in-flight drain for the global setup callback.
- A dispatch paused after loading the raw cookie but before `syncCallback` successfully creates/promotes the weak reference is not protected by the later strong reference. If the Device is destroyed and its storage reclaimed before that dispatch reaches `createWeak`, the bridge may dereference a stale cookie while trying to obtain the weak reference.

This is a **static race surface**, not proof that a particular runtime interleaving or use-after-free occurs. The HIDL callback strong reference at `Device+0x24` protects the callback interface object, not the raw Device cookie before promotion.

## Gate decision

- `syncCallback` weak-reference creation: **confirmed**.
- `syncCallback` weak-to-strong promotion attempt: **confirmed**.
- Strong Device reference held during callback access/invocation after successful promotion: **confirmed**.
- Strong/weak release calls on normal cleanup: **confirmed**.
- Atomic callback/cookie pair registration and replacement: **not proven**.
- Protection of raw cookie between factory load and successful promotion: **not proven**.
- Factory in-flight dispatch drain before Device destruction: **not proven**.
- Runtime race/UAF occurrence: **not tested / not claimed**.

Keep P2 **OPEN**. Keep Primary HAL fail-closed and `installable: false`. Do not integrate the setup callback path into AudioCore or treat the in-body promotion as a substitute for a factory-side admission/drain contract. This channel remains distinct from the vector-backed `0xd4/0xd8` parameter-change registry.

## 2026-11-14 — constructor callsite parser correction

A review of `audit_primary_hal_p2_hidl_device_lifetime.py` found that its direct-call and relocation-offset regular expressions were over-escaped. The direct-call parser therefore did not reliably recognize objdump `BL/BLX` records, so the earlier derived-constructor callsite inventory must not be treated as complete.

The parser now uses compiled expressions for:
- direct `BL` and `BLX` instructions with an immediate symbolized target;
- relocation records with hexadecimal offsets;
- normalization of Thumb target bit 0.

Four regression tests cover the known `DeviceC1` branch to veneer address `0x000281dc`, an ARM `BLX` target with the Thumb bit set, unresolved register-indirect `BLX`, and relocation offset parsing. The test module is included in both workflow sparse checkout and the contract job's unittest discovery.

The regex fixtures were checked locally against representative objdump lines. This is not a substitute for the pending GitHub Actions run against the pinned HIDL ELF. Until that run confirms the full derived-constructor callsite inventory, do not promote the earlier negative callsite result to proof that no derived constructor invokes `DeviceC1`.

P2 remains **OPEN**. The raw-cookie pre-promotion window and factory-side in-flight drain are unchanged and remain hard gates.
The RefBase collector now labels successful symbol coverage as `REFBASE_CANDIDATE_SYMBOL_STATUS: COMPLETE` and separately emits `REFBASE_RUNTIME_SELECTION_STATUS: UNRESOLVED`. The former means both candidate `libutils.so` files expose and disassemble the required symbols; it does not select which library the vendor linker namespace loads at runtime.
