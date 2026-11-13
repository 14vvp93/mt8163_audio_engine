# Hardware identity correction — CD3310EO

Date: 2026-11-13
Repository: `14vvp93/mt8163_audio_engine`
Branch: `integration/beta-0.1`
Status: user-confirmed physical hardware fact; firmware reconciliation pending.

## Authoritative physical-board fact

The user has explicitly confirmed that the head unit physically contains **CD3310EO only**. CD3313, AK7604 and FY7604 are not physically populated in this unit.

This physical-board statement supersedes all earlier assumptions that CD3313 is the target audio processor. It does not invalidate the existence of CD3313-related files, binaries, module variants, DTBO nodes, JNI wrappers or protocol-analysis artifacts in the supplied firmware. Those artifacts must now be treated as firmware references or possibly dormant/legacy code until their relationship to the populated CD3310EO is established.

## Immediate engineering consequences

1. CD3313 is not the target hardware protocol for this head unit.
2. CD3313 emulator, parser, fuzzing and driver audits remain useful as historical firmware-analysis work, but must not be described as validation of CD3310EO.
3. Do not enable, port or issue CD3313 commands to the physical unit.
4. Do not assume that a DTBO node named `hcn,cd3313` or an init-script reference to `cd3313.ko` proves the physical chip identity. These can represent dormant, shared-image or legacy support.
5. P0 must pivot to identifying the CD3310EO control interface and its actual ownership in this firmware: exact driver/module, I2C client/address, register map or command framing, GPIO/reset/mute wiring, userspace/JNI/HAL callers, and audio route.
6. All investigation remains read-only. No module loading, ioctl, I2C write, GPIO change, mixer write, flashing or physical probing is authorized by this correction.

## Evidence classification

- **Physical population:** CD3310EO only — explicitly confirmed by the user.
- **CD3313-related firmware artifacts:** present in supplied firmware analyses; physical applicability is disproven for this unit unless a separate non-populated board variant is under study.
- **CD3310EO driver/protocol/register map:** not yet established from repository evidence.
- **Runtime device ownership and audio routing:** not yet established.
- **Android HAL ABI / device validation:** still open.

## Revised P0 sequence

- [ ] Search the exact `14vvp93` firmware dump and analysis artifacts for CD3310EO markings, aliases, driver strings, I2C IDs, register tables and command interfaces.
- [ ] Identify candidate kernel modules and compare their strings, symbols, imports, device-tree matches and probe functions against the CD3310EO identity.
- [ ] Trace the userspace path from Audio HAL/JNI/Binder to the candidate driver; do not assume `/dev/audiomux` is the correct interface.
- [ ] Recover the CD3310EO protocol/register semantics from matching firmware evidence, documenting confidence and unresolved fields.
- [ ] Update the canonical roadmap and mark CD3313 production transport as out-of-scope for this physical unit.
- [ ] Keep production hardware transport detached until the exact driver, framing, board wiring and runtime ownership are proven.

## Scope note

This decision applies to the user's physical head unit. It does not assert that every MT8163 firmware/board variant uses CD3310EO, nor does it identify the exact firmware component that controls this chip.
