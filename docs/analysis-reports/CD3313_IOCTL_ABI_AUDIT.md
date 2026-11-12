# CD3313 ioctl and character-device ABI audit

Date: 2026-11-12  
Target analyzed: vendor archive module `vendor_lib/lib/modules/cd3313.ko`  
Build ID: `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`

## 1. ioctl control flow

The vendor module's `cmd_ioctl` extracts the encoded size from `_IOC_SIZE`, multiplies it by four, caps the copied payload at 256 bytes, allocates a command buffer, copies the payload from userspace, and submits it to `post_event_delayed` with event type 1.

The return value from `post_event_delayed` is not checked. The ioctl then returns success after queue submission; it does not wait for the command callback or I2C transaction to finish. Commands `0x55` and `0x5C` receive an extra 30 ms sleep, but that is not a completion acknowledgement.

**Implication:** a successful JNI/ioctl return means the request was accepted/copied and queued, not that the DSP accepted the register write. I2C errors in the worker are not propagated synchronously to the caller.

The special `0xF0` query path is separate: it copies an 8-byte chip-identification response back to userspace.

## 2. Payload-length validation findings

The callback decodes the command type from the header and reads payload words from offsets 4, 8, 12 and 16 bytes after the command header. The following checks were observed in the vendor module disassembly:

| Command | Callback behavior | Validation |
|---|---|---|
| `0x01` input channel | Reads first payload word | Checks encoded word count is nonzero |
| `0x03` input gain | Reads payload words at +4 and +8 | **No visible minimum-length check before reads** |
| `0x04` input mute | Reads three payload words | Checks count > 2 |
| `0x09` command | Reads payload word at +4 | **No visible minimum-length check before read** |
| `0x10` master volume | Reads first payload word | Checks count is nonzero |
| `0x11` master mute reason | Reads two payload words | Checks count > 1 |
| `0x12` volume percentage | Reads payload | Checks count > 1 |
| `0x15` four output levels | Reads four payload words at +4, +8, +12, +16 | **Checks only count > 1; under-validates payload length. The paired vendor callback range-checks all four indices against 0..15.** |
| `0x20` mixing mode | Reads payload | Checks count > 2 |
| `0x55` bass | Reads payload | Checks count > 1 |
| `0x56` loudness/equalizer control | Reads payload word at +4 | **No visible minimum-length check before read** |
| `0x5C` treble | Reads payload | Checks count > 1 |
| `0xA3` subwoofer GPIO | Reads payload word at +4, then changes GPIO | **No visible minimum-length check before read** |
| `0xF1` DSP shutdown/wakeup | Reads first payload word | Checks count is nonzero |

The highlighted cases can consume uninitialized bytes from the command allocation when userspace supplies a short payload. The ioctl path allocates with `kmem_cache_alloc` (not a zeroing allocator), stores the header and copies only the encoded payload length; it does not clear the remainder after a successful short copy. The callback can therefore read stale slab contents as missing command fields. The exact effects depend on the JNI caller's encoded `_IOC_SIZE` and array construction, which must be reviewed alongside this kernel code.

## 3. Status is cached state, not physical readback

The vendor module's `cmd_read` builds a textual status report using cached global fields and `sprintf`, then returns it through `simple_read_from_buffer`. The function contains no I2C read transaction.

**Implication:** values read from the device node are the driver's software cache, not a confirmed readback of the CD3313 chip. After a reset, failed write, out-of-band command or lost power event, cached status can differ from physical DSP state.

## 4. Raw character-device write path

The vendor `cmd_write` path:

- clears a 12-byte local buffer;
- calls `simple_write_to_buffer` using the file's persistent `ppos`;
- parses the local buffer with `kstrtouint` in base 16;
- sends one byte over I2C, requiring the result to equal 1;
- retries up to five times with 20 ms delays and updates the register cache only after success.

Two input-handling risks remain:

1. `simple_write_to_buffer` advances the persistent file position. The local buffer is freshly cleared on each write and the handler does not reset `ppos`; multiple writes through the same open file descriptor can therefore write at a nonzero offset in a fresh buffer and parse a zero-prefixed value.
2. The buffer is 12 bytes and is zeroed before copying, but a full 12-byte write can overwrite every byte. No explicit terminator is appended before `kstrtouint`, so the maximum-length input may not be NUL-terminated.

These should be fixed by parsing a bounded local copy with an explicit terminator and either resetting/rejecting nonzero offsets or using a write interface whose semantics are intentional.

## 5. Confirmed versus pending

### Confirmed from the vendor module

- asynchronous command queue; ioctl does not report DSP/I2C completion;
- no visible payload-size check for `0x03`, `0x09`, `0x56` and `0xA3`;
- `0x15` reads four words but validates only a count greater than one; the paired vendor callback additionally checks every loaded index is <= 15.
- read/status path reports cached state rather than I2C readback;
- raw write path has persistent-position and maximum-length termination risks;
- the vendor build's normal and raw I2C write helpers do retry and validate one-byte transfer success (see [module variant comparison](CD3313_MODULE_VARIANT_DIFF.md)).

### Still to reconcile with userspace

- exact JNI `SendCmdAudioMux(int, int[])` command construction and encoded size;
- whether callers open/close `/dev/audiomux` for each raw write;
- command-specific payload structures and valid ranges;
- whether the userspace stack depends on the asynchronous return contract.

## 6. Recommended rewrite requirements

1. Define a command descriptor table with exact minimum/maximum payload lengths and typed fields.
2. Reject malformed or short payloads before queueing any work.
3. Keep one serialized command worker, but provide a completion/result mechanism where userspace needs confirmation.
4. Separate desired state, last successfully written state and physical readback state.
5. Make raw diagnostic writes bounded, explicitly terminated, offset-independent and permission-restricted.
6. Preserve the existing ioctl/JNI ABI until every userspace caller has been inventoried; add a versioned ABI for future changes.

Static disassembly only. No device access, module loading, I2C writes or GPIO changes were performed.
