# CD3313 vendor callback revalidation

Date: 2026-11-12

## Evidence identity

- Firmware member: `firmware/extracted/vendor/lib/modules/cd3313.ko`
- SHA-256: `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594`
- ELF Build ID: `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`
- ELF: ARM32, EABI5, with DWARF, not stripped
- Analysis source: GitHub Actions artifact `cd3313-current-module-analysis`, run `43238133570`
- Artifact file: `disassembly.txt`, function `event_callback` at `0x1504`; `cmd_ioctl` at `0x0200`

This report supersedes conflicting statements about the installed-module candidate where they are contradicted by this exact disassembly. It is static analysis only; no module was loaded and no device I/O was performed.

## 1. ioctl framing confirmed in the paired vendor build

At `cmd_ioctl`:

- Extracts the 14-bit size field from request bits 16..29.
- Multiplies it by four to obtain payload bytes.
- Allocates a command-cache object.
- Stores the original 32-bit request word at offset 0.
- Passes the payload destination at offset +4 to `__check_object_size` and `arm_copy_from_user`.
- Caps the copied payload at 256 bytes.
- Queues the command object as event ID 1.

The object allocation size is 256 bytes. A 256-byte payload after the four-byte header would require 260 bytes in total. The call to `__check_object_size` is present; whether this terminates the kernel depends on the exact vendor hardened-usercopy implementation/config. The host parser's 252-byte maximum remains the conservative object-boundary policy.

The ioctl return is not a completion acknowledgement. The event is posted asynchronously; the callback's result and I2C completion are not returned to the caller.

## 2. Vendor callback command map

The table below records the exact size-field guard visible in this vendor callback, the words read, and the host-side safe policy. A guard is a condition on the 32-bit-word size field, not proof that all handler arguments are present.

| ID | Vendor callback guard | Words read | Host-side safe policy |
|---|---|---|---|
| `0x01` | size > 0 | +4 | Require 1 word |
| `0x03` | no visible size guard | +4, +8 | Require 3 complete words; replacement additionally restricts channel 0..6 and gain 0..3 |
| `0x04` | size > 2 | +4, +8, +12 | Require 3 words |
| `0x09` | no visible size guard | +4 | Sets `volume_max`; default is 40 and `set_master_volume()` uses it as the upper bound |
| `0x10` | size > 0 | +4 | Require 1 word |
| `0x11` | size > 1 | +4, +8 | Require 2 words |
| `0x12` | size > 1 | +4 | Require 2 words; second word remains unresolved |
| `0x15` | size > 1 | +4, +8, +12, +16 | Require 4 words and validate all four indices 0..15 |
| `0x20` | size > 2 | +4 | Require 3 words |
| `0x55` | size > 1 | +8 | Require 2 words |
| `0x56` | no visible size guard | +4 | Require 1 word |
| `0x5C` | size > 1 | +8 | Require 2 words |
| `0xA3` | no visible size guard | +4 | Require 1 word |
| `0xF0` write direction | no payload read; size-zero error branch | none | Do not model as a write operation; read-direction `0xF0` is a separate 8-byte static identifier query |
| `0xF1` | size > 0 | +4 | Require 1 word |

Unknown command IDs are dropped by the callback's default path after the queued object is freed. The special `0x56` and `0xA3` branches are outside the 12-entry command-length table. The `0x09` branch stores the supplied word into the `volume_max` data symbol at `.data + 0x5c`. The paired `set_master_volume()` function loads this field and rejects requested steps above it. The initial `.data` value is 40. Because the volume lookup table contains 41 entries (indices 0..40), the replacement validates `volume_max <= 40` before accepting the command; the factory callback itself does not visibly enforce that upper bound.

## 3. Corrected command 0x15 finding

The vendor callback checks the size field against 1, then loads four 32-bit values from offsets +4, +8, +12 and +16. It performs an unsigned comparison of **each** value against 15 before indexing the 16-entry speaker attenuation table. If any index is greater than 15, the command is dropped before table access.

Therefore the earlier statement that this vendor callback lacked range checks for all four indices was incorrect. The exact paired vendor module does range-check all four values.

The remaining defect is the short-payload condition: a size field of 2 or 3 passes the driver's `size > 1` check, while the callback still reads four words. Missing words are not initialized by the command copy path on a successful short copy and can therefore come from stale slab contents. This is distinct from out-of-range table indexing.

The replacement parser intentionally requires four complete words and rejects any index outside 0..15. Tests cover each of the four invalid index positions independently.

## 4. Other short-payload hazards confirmed

The exact vendor callback has no visible size-field guard before these loads:

- `0x03`: reads two words (+4 and +8).
- `0x09`: reads one word (+4).
- `0x56`: reads one word (+4).
- `0xA3`: reads one word (+4), then requests raw GPIO 409 state.

The host parser must continue to require complete words for these commands, even where the factory callback does not. This is a deliberate fail-closed replacement contract, not an attempt to reproduce the factory's unsafe short-frame behavior.

For `0x03`, the old 12-pair table records a three-word threshold, while the vendor callback's optimized branch contains no visible local length guard before its two loads. Keep the host parser's three-word threshold as a conservative policy; do not describe it as a proven vendor callback minimum. The replacement also validates channel 0..6 and gain 0..3 before accepting the command. These are fail-closed host constraints derived from the seven-entry channel state and two-bit gain field; they are not claimed as proven vendor-side checks.

## 5. JNI compatibility

The matching JNI bridge encodes `array_length + 1` words. For command `0x15`, four Java values therefore produce a five-word request. The extra word is outside the four values consumed by the callback. A replacement JNI bridge must allocate and initialize this compatibility word itself if it preserves the legacy request shape; it must never read beyond the Java array.

The host parser preserves an explicitly supplied fifth word and does not synthesize it. Typed builders encode only explicitly supplied arguments and are not the legacy JNI compatibility adapter.

## 6. Closure items

- [ ] Reconcile the complete vendor callback control flow, including channel/gain value checks for `0x03` and remaining branch-specific side effects.
- [ ] Inventory all Java/Binder callers and their exact arrays.
- [ ] Confirm vendor `__check_object_size` and hardened-usercopy behavior from the exact kernel source/config.
- [ ] Preserve strict complete-payload checks in the replacement parser.
- [ ] Keep production transport detached until framing, ownership and board topology are proven.

No ioctl, I2C write, GPIO change, module load or physical-device test was performed.
