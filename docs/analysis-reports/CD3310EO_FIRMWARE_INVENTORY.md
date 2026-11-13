# CD3310EO firmware inventory — initial read-only pass

Date: 2026-11-13  
Target repository: `14vvp93/public_calculations`  
Target device: MT8163 / Android 9 head unit  
Physical audio IC: **CD3310EO only** (user-confirmed)

## Scope

This pass inspected the available text files and directory entries under `firmware/dumps/mt8163`. It did not modify or load any firmware. The GitHub connector could list the binary module but could not decode its bytes as UTF-8, so no binary disassembly is claimed.

## Confirmed repository artifacts

| Artifact | Evidence | Meaning |
|---|---|---|
| `kernel/cd3313.ko` | 183,368 bytes; blob SHA `5c63e6ef16f0b0e46a33eabc75fb6cd66f67afe7` | A CD3313-named kernel module is present in the dump. Its actual probe/IC compatibility has not been established in this pass. |
| `init.mt8163.rc` | Lines 563–566 | Boot init requests `cd3313.ko`, `ak7604.ko`, `fy7604.ko`, `si475x.ko`. This does not prove successful module load or physical IC population. |
| `factory_init.rc` | Lines 530–537 | Factory init also requests CD3313/AK7604/FY7604 module variants. |
| `ueventd.rc` | Line 139 | Permission rule exists for `/dev/audiomux`; not proof of runtime ownership. |
| `build.prop` | Full text scan for `CD3310`, `CD3310EO`, `audio` processor terms | No matching strings found in this file. |

## Key interpretation

The firmware's CD3313/AK7604/FY7604 support files conflict with the user-confirmed physical board population if interpreted as a literal board inventory. The correct conclusion is **not** that the user is mistaken, nor that CD3310EO is CD3313-compatible. The init scripts may be shared across board variants, the named module may use legacy naming, or the actual CD3310EO path may be implemented elsewhere. Those alternatives remain hypotheses.

The existence of `/dev/audiomux` permission rules and module insertion commands is insufficient to identify the real physical chip or its I2C protocol.

## Next investigation

1. Enumerate the entire firmware dump, including nested `vendor/lib/modules`, `vendor/lib/hw`, `vendor/bin`, `system/lib`, `system/lib/hw`, VDEX/ODEX/APK and DTB/DTBO content.
2. Extract printable strings and ELF metadata from every audio-related `.ko`, `.so`, service and executable.
3. For each candidate kernel module, recover `MODULE_DEVICE_TABLE`, I2C IDs, OF compatible strings, probe function, I2C address handling and registered misc/char device.
4. Inspect the CD3313-named module itself to test whether it contains a CD3310EO ID/alias/register map despite the filename. Until then, its compatibility is unknown.
5. Trace JNI/Binder/HAL callers to the candidate module and correlate their command/register payloads.
6. Compare candidate DTB/DTBO nodes with the exact board variant and user-confirmed chip marking.

## Safety boundary

No guessed I2C address, register, ioctl, module load, GPIO operation, mixer write or flash operation is permitted. Keep all work read-only until driver identity, bus ownership, board wiring and protocol are independently established.

## Evidence labels

- Physical IC population: **USER CONFIRMED** — CD3310EO only.
- CD3313 module presence in dump: **STATIC FILE INVENTORY VERIFIED**.
- CD3313 module compatibility with CD3310EO: **UNKNOWN**.
- Runtime `/dev/audiomux` owner: **UNKNOWN**.
- CD3310EO I2C address/register map/protocol: **UNKNOWN**.
- Hardware/device validation: **NOT PERFORMED**.
