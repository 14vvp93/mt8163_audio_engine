# CD3313 driver static risk audit

## Scope

This audit combines earlier standalone-module findings with vendor-kernel context. The standalone reference module is Build ID `dae15c9eb019e9388d15cb88088c202d017ebcf0`; the firmware-paired vendor module is Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`. Do not apply findings across these variants without checking the exact build. Direct revalidation of the paired vendor callback is recorded in [CD3313_VENDOR_CALLBACK_REVALIDATION.md](analysis-reports/CD3313_VENDOR_CALLBACK_REVALIDATION.md). No malformed requests have been sent to a physical head unit. Findings below are static-analysis results, not device-tested exploits.

Primary evidence:
- [Focused module analysis run 42883809539](https://github.com/14march93/mt8163_audio_engine/actions/runs/42883809539)
- [Character-device ABI](CD3313_CHAR_DEVICE_ABI.md)
- [Ioctl ABI](CD3313_IOCTL_ABI.md)
- [Command dispatch map](CD3313_COMMAND_DISPATCH.md)

## Findings

| ID | Finding | Confidence | Potential effect |
|---|---|---|---|
| R1 | Ioctl slab object is 256 bytes, but the copy cap is 256 bytes after a 4-byte header | High, conditional on vendor hardened-usercopy implementation | A 64-word-or-larger payload can exceed the slab object and trigger a kernel BUG |
| R2 | Commands 0x03, 0x09, 0x56 and 0xA3 load payload words without a size check | High | Short requests consume stale slab data; effects include input gain, volume limit, loudness and raw GPIO state |
| R3 | Command 0x15 validates only a two-word minimum but reads four words | High | Two/three-word requests consume stale payload words |
| R4 | The paired vendor module range-checks all four 0x15 indices against 0..15 before table access; the earlier no-check claim is not valid for this build | High | The remaining confirmed issue is short-payload stale-word consumption, not out-of-range table indexing |
| R5 | Raw write() uses a 12-byte zeroed buffer, copies up to all 12 bytes, then calls kstrtouint() without adding a terminator | High, assuming the Linux 4.9.117 helper behavior | A full 12-byte valid-hex input can make the parser inspect beyond the local buffer |
| R6 | Raw write() preserves the VFS file position; after the first write, a later write on the same fd starts at a nonzero offset in a freshly zeroed buffer | High | Subsequent write() calls on the same open descriptor are parsed as an empty string and silently do nothing |
| R7 | The older standalone raw-write path ignores transfer status; the paired vendor build checks exact one-byte success and retries up to five times before updating cache | High for the variant difference | Do not attribute the standalone cache-consistency defect to the paired vendor module |
| R8 | ioctl write returns success after queueing; callback result and post_event_delayed() result are not propagated | High | Userspace cannot treat ioctl success as proof that a command ran |
| R9 | cmd_read() reports cached register bytes and software state, not hardware readback | High | A stale cache can look like successful hardware state |
| R10 | Command 0x03 rejects channel -1 and 6 but does not bound values above 6 before indexing the 7-byte `channel_gain` array | High | Channel values 7+ can write beyond the array into adjacent global state |

## Details and evidence

### R1 — ioctl object-size mismatch

dsp_probe() creates cmd_cachep with a 256-byte object. cmd_ioctl() writes the 4-byte request header at offset 0, then allows up to 256 payload bytes to be copied at offset +4. The module imports and calls __check_object_size() before arm_copy_from_user().

In upstream Linux 4.9.117, __check_object_size() checks heap-object bounds and report_usercopy() calls BUG() on an invalid range:
- [mm/usercopy.c, Linux 4.9.117](https://github.com/gregkh/linux/blob/v4.9.117/mm/usercopy.c)

If the vendor kernel retains this behavior, size_field >= 64 (256 payload bytes) exceeds the 256-byte object by four bytes and can trigger a kernel BUG. The maximum payload that fits after the header is 252 bytes (63 words). The emulator now rejects payloads above 252 bytes with UnsafePayloadSize. The exact vendor helper implementation still needs source-level confirmation.

### R2 — commands 0x03, 0x09, 0x56 and 0xA3 short payloads

The paired vendor callback has no visible size-field guard before these payload reads. `0x03` reads two words (+4 and +8); `0x09`, `0x56` and `0xA3` each read one word (+4). cmd_ioctl() does not zero the slab object. Short successful copies can therefore leave missing words sourced from stale slab contents. The replacement parser requires complete payloads and intentionally does not reproduce stale-memory behavior.

### R3/R4 — command 0x15

The command-length table requires two words, and the callback checks only size_field > 1, but it then loads four 32-bit values at +4, +8, +12, and +16. Missing values in a two/three-word request are stale slab contents.

The paired vendor module compares each of the four unsigned indices against 15 before indexing the 16-entry cd3313_speaker table. The previous no-range-check statement is superseded by the direct vendor callback disassembly. The short-payload problem remains: a two- or three-word frame passes the driver's size check, then the callback reads four words. The emulator requires four complete words and validates all four indices.

### R10 — command 0x03 channel index can exceed channel_gain

In the paired vendor `event_callback` branch for command `0x03` (0x1820..0x1858), the callback rejects channel `-1` and channel `6`. It does not reject channel values greater than 6 before using the channel value as an offset into the seven-byte `channel_gain` object at `.data + 0x28`. Values 7+ can therefore write beyond the array into adjacent global state. The gain value is masked with `& 3`; the replacement accepts only 0..3 and only channels 0..5. This is a replacement-side validation, not a claim that the factory callback has equivalent bounds checks.

### R5/R6/R7 — raw write()

cmd_write() copies at most 12 bytes into a 12-byte stack buffer, then calls kstrtouint(base=16). The Linux 4.9.117 simple_write_to_buffer() helper does not append a NUL terminator, while kstrtouint() requires a NUL-terminated string:
- [fs/libfs.c, Linux 4.9.117](https://github.com/gregkh/linux/blob/v4.9.117/fs/libfs.c)
- [lib/kstrtox.c, Linux 4.9.117](https://github.com/gregkh/linux/blob/v4.9.117/lib/kstrtox.c)

A full 12-byte input can therefore leave the local string unterminated. The function also passes the VFS file-position pointer through unchanged; after the first write, a subsequent write on the same fd copies into a nonzero offset of a newly zeroed buffer and parsing sees a leading NUL. Finally, the function ignores the I²C send result, updates the cache anyway, and returns the original userspace count even on parse or transfer failure.

The emulator's write_raw_command_text() safely models the hexadecimal parser, original-count result, file-position behavior, and one-write-per-open limitation. It rejects a full 12-byte buffer instead of reproducing the possible stack over-read.

### R8/R9 — asynchronous success and cache-only read

The ioctl wrapper returns after requesting event-queue dispatch and does not propagate callback completion or queue-post failure. Unknown IDs reach the callback default path and are freed/dropped. cmd_read() prints reg_bufs and driver globals without performing I²C readback. A successful ioctl or a changed text report is therefore not proof that the CD3313 hardware accepted the command.

## Safe client rules

For any future userspace controller:

1. Keep write-direction ioctl payloads at or below 252 bytes (63 words).
2. Always send one complete 32-bit word for 0x56 and 0xA3.
3. Send exactly four complete words for 0x15; validate every index is 0..15.
4. Use only recovered command IDs and their documented minimum/required payloads.
5. Treat ioctl success as “queued”, not “executed”; verify through a trusted status path and, ultimately, hardware measurement.
6. Avoid raw text write() for production control. If it must be used for compatibility testing, open/close per command and keep input shorter than 12 bytes.
7. Do not test malformed or oversized requests on the physical head unit; they may crash the kernel or change GPIO state unpredictably.

## Still unresolved

- Exact vendor implementation of handler_thread.h and event-queue cancellation/coalescing.
- Exact vendor implementation of hardened-usercopy, despite the imported __check_object_size symbol.
- Exact userspace ioctl macro/type and the low eight request bits.
- Physical GPIO polarity, board wiring, and actual audio routing.
- Hardware confirmation of all register and timing behavior.
