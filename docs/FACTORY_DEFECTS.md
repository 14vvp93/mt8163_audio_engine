# Factory defect and risk register

This register tracks defects and unresolved architectural risks found while reconstructing the factory MT8163 audio stack. A static finding is not a claim that the failure has occurred on a live head unit. Every fix must have a regression test and preserve the verified Android/userspace contract.

## Confirmed static defects

| ID | Severity | Component | Finding | Required correction | Status |
|---|---|---|---|---|---|
| CD3313-ABI-001 | High | `cd3313.ko` ioctl | Request/object allocation and copy sizes do not agree; the encoded ioctl frame can cause a larger copy than the allocation | Decode and validate frame length before allocation/copy; test boundary and overflow cases | Open |
| CD3313-ABI-002 | High | `cd3313.ko` ioctl | Commands `0x56` (Loudness) and `0xA3` (subwoofer GPIO) consume payload without a sufficient short-frame check | Enforce command-specific minimum/exact argument counts before reading | Open |
| CD3313-ABI-003 | High | `cd3313.ko` ioctl | Command `0x15` consumes four words despite a weaker length threshold | Require the complete four-word payload | Open |
| CD3313-ABI-004 | High | `cd3313.ko` speaker levels | Command `0x15` does not fully validate speaker-table indices | Validate every index against the recovered table bounds before state or I/O changes | Open |
| CD3313-RAW-001 | High | `cd3313.ko` raw write | Text parser may consume a buffer without guaranteed NUL termination | Use bounded parsing with explicit input length; reject malformed and oversized input | Open |
| CD3313-RAW-002 | Medium | `cd3313.ko` raw write | File-position handling makes repeated writes unreliable | Define and test write-position semantics; do not silently depend on stale `f_pos` | Open |
| CD3313-I2C-001 | High | `cd3313.ko` raw write | I²C transfer status is ignored and software cache can be updated after a failed transfer | Propagate transfer errors; update cache only after confirmed successful transfer | Open |
| CD3313-ASYNC-001 | Medium | `cd3313.ko` ioctl/event queue | ioctl success can mean only that a command was queued, not that hardware execution succeeded | Preserve legacy ABI while exposing completion/error state to the owning userspace client | Open |
| CD3313-STATE-001 | Medium | `cd3313.ko` status reporting | Diagnostic status exposes cached state rather than physical chip readback | Label cache as requested/last-written state and expose validity/unknown state; never call it hardware readback | Open |
| CD3313-JNI-001 | High | Vendor JNI bridge | JNI encodes one more 32-bit word than the Java array contains, allowing a native out-of-bounds read when the kernel copies the encoded length | Replace/fix the bridge to build a correctly sized initialized frame; kernel-only changes cannot repair the caller's over-read | Open |

## Unresolved risks — not yet classified as defects

| ID | Component | Evidence | What must be verified |
|---|---|---|---|
| AUDIO-MODULE-001 | CD3313 / FY7604 / AK7604 modules | All three modules contain an `audiomux_misc_device` and call `misc_register` | Confirm board/module selection and whether more than one variant can be loaded. Treat a shared misc-device name as a potential collision, not a proven runtime failure |
| CARSTATUS-AUDIO-001 | `carstatus.ko` | `update_mute` contains a `filp_open` relocation and the `/dev/audiomux` path | Recover the exact operation and determine whether this kernel-to-userspace device dependency is active |
| FRAMEWORK-AUDIO-001 | `libaudioflinger.so` / `libaudiopolicymanagerdefault.so` | Both reference `/dev/audiomux` and `/dev/carstatus` | Recover exact call sites and callers; replace factory-specific hooks without breaking Android framework APIs |
| HFP-COMPAT-001 | Primary HAL / HFP vendor libraries / NVRAM | HFP downlink/uplink, echo-reference threads, HFP HIDL methods and `AUDIO_HFP_PARAM_STRUCT` are present | Map full call/microphone path, sample-rate/volume controls, echo reference and parameter persistence |

## Evidence sources

- [CD3313 static driver risk audit](CD3313_DRIVER_RISK_AUDIT.md)
- [CD3313 ioctl ABI](CD3313_IOCTL_ABI.md)
- [CD3313 driver rewrite plan](../kernel/cd3313/README.md)
- [Firmware binary analysis summary](repository-audit/FIRMWARE_ANALYSIS_SUMMARY.md)

## Closure rules

A defect is closed only when:
1. the root cause is understood and recorded;
2. the fix is implemented in the appropriate subsystem;
3. a regression test covers the defect and its boundary cases;
4. compatibility impact is reviewed;
5. build/test evidence is recorded separately from target-device validation.

Do not mark static findings as fixed based only on a successful host build. Do not change undocumented hardware behavior to silence an error.
