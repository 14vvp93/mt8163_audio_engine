# CD3313 ioctl ABI and event-queue reconstruction

## Evidence identity and scope

This document is based on the ARM32 cd3313.ko analysis artifact produced by focused workflow run [42878596077](https://github.com/14march93/mt8163_audio_engine/actions/runs/42878596077). The follow-up artifact [42883809539](https://github.com/14march93/mt8163_audio_engine/actions/runs/42883809539) adds the raw DWARF file table and the .rodata.str1.4 strings used to reconstruct cmd_read(), cmd_write(), and the driver channel labels.

- Module SHA-256: b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb
- ELF: 32-bit little-endian ARM, EABI5, relocatable module with DWARF.
- Embedded source unit: drivers/misc/cd3313/cd3313.c
- Kernel target identified by the existing reconstruction: Linux 4.9.117+.

This is static reverse engineering. The event-queue implementation is imported from another kernel component and is not present in this module. No physical head-unit validation has been performed.

## 1. ioctl request-word layout

cmd_ioctl() extracts these fields from the 32-bit request word:

| Bits | Width | Driver interpretation |
|---|---:|---|
| 30..31 | 2 | Direction: 1 = write, 2 = read |
| 16..29 | 14 | Size field |
| 8..15 | 8 | Command/status ID |
| 0..7 | 8 | Not inspected by the recovered dispatcher |

The command ID is taken from the Linux _IOC_TYPE position, not the low _IOC_NR byte. The driver does not inspect bits 0..7 in the recovered cmd_ioctl() path. The native caller now confirms that it sets this low request-number byte to zero for both the write commands and the read-only chip-identification request.

For clarity, the recovered word can be represented symbolically as:

```text
request = (direction << 30)
        | (size_words << 16)
        | (command_id << 8)
        | low_nr
```

This is the recovered bit-field decomposition. The native bridge confirms `direction=1`, `low_nr=0`, `command_id` in the conventional `_IOC_TYPE` byte, and a word-count size field. The native request formula is documented in the Java/Binder caller correlation below; do not replace it with a conventional `_IOW(type, nr, struct)` guess.

The size field is treated as a count of 32-bit words: copied bytes = size_field * 4. The write path caps the copied byte count at 256 bytes.

## 2. Write-direction ABI

For direction 1:

1. Allocate one object from the driver's cmd_cachep slab cache.
2. Store the original 32-bit ioctl request word at object offset +0.
3. Copy min(size_field * 4, 256) bytes from the userspace argument pointer to object offset +4.
4. Call print_log() with the resulting object.
5. Request immediate dispatch with post_event_delayed(handler, 1, object, 0).
6. For command IDs 0x55 or 0x5C only, sleep for 30 ms after posting.

The callback therefore receives a buffer with the original request word at offset 0 and the first command argument at offset +4. Subsequent 32-bit arguments are at +8, +12, and +16.

The callback extracts the ID and size from the prepended request word. It does not receive a second nested header.

Important stage distinction: cmd_ioctl() itself does not validate the command ID against cmd_len_list and does not enforce the per-command minimum size. For write direction it copies the capped payload, logs it, and queues event ID 1. The callback then performs command-specific dispatch and applies cmd_len_list checks for the 12 listed commands. The extra 0x56 and 0xA3 branches are handled outside that 12-pair table. Therefore the table is callback-side validation, not an ioctl-entry whitelist. Unrecognized IDs reach the callback's default branch, where the queued object is freed without a command handler action. cmd_ioctl() has already returned success after posting, so userspace receives no callback result or synchronous confirmation that the command executed. The post_event_delayed() return value is also ignored.

### Short-payload hazard for commands 0x56 and 0xA3

The event_callback() branches for 0x56 (loudness) and 0xA3 (subwoofer GPIO) load the first payload word at offset +4 without checking the size field. These IDs are not in cmd_len_list. cmd_ioctl() allocates the 256-byte cache object without zeroing it, writes the request header, and copies only the declared payload bytes. A write-direction request with size=0 therefore leaves the word at +4 as stale slab data, which the callback then consumes:
- 0x56 can unexpectedly enable or disable loudness.
- 0xA3 can unexpectedly change the subwoofer GPIO raw level.

This is an uninitialized-heap-data use in the callback path. It is not a confirmed privilege escalation or arbitrary memory disclosure, but it is a real malformed-request hazard. The emulator deliberately rejects 0x56 and 0xA3 frames that do not contain one complete 32-bit payload word; it does not reproduce the driver's stale-memory behavior.

The 0x15 four-channel level handler has a related but separate mismatch: cmd_len_list requires only two words, and the callback checks only that the size field is greater than one before loading four 32-bit indices at offsets +4, +8, +12, and +16. A two- or three-word request therefore leaves one or two indices sourced from stale slab contents. The handler then uses each value as an offset into the 16-entry cd3313_speaker table; no complete 0..15 validation of all four values was found in this branch. Invalid indices can read beyond that table and feed an unintended low-five-bit value into the I²C register write. The emulator imposes the safe contract: four complete words, every index in 0..15.



### Command 0xF1 power transition

Unlike 0x56/0xA3, command 0xF1 is present in cmd_len_list with a one-word minimum, and its callback checks the size before reading the payload. The first word is boolean-like: zero calls dsp_shutdown() unconditionally; nonzero requests wakeup and calls dsp_wakeup.part.1() only if the driver's shutdown flag is set. A nonzero request while already awake does not replay the wakeup sequence. The emulator now models this command using the recovered shutdown and wakeup journals.




### Important length-validation distinction

The DWARF cmd_len_list is exactly 12 (command ID, minimum size-field) pairs:

| Command ID | Driver minimum size field |
|---:|---:|
| 0x01 | 1 |
| 0x03 | 3 |
| 0x04 | 3 |
| 0x10 | 1 |
| 0x11 | 2 |
| 0x12 | 2 |
| 0x15 | 2 |
| 0x20 | 3 |
| 0x55 | 2 |
| 0x5C | 2 |
| 0xF0 | 1 |
| 0xF1 | 1 |

These are thresholds on the size field, not exact argument counts. Several handlers consume fewer words than the threshold. One handler consumes more:

- 0x15 reads four 32-bit speaker indices at offsets +4/+8/+12/+16, but its table threshold is only 2 words. A frame with the minimum 2-word payload would not contain all four arguments. This is a static under-validation/bounds warning. The host parser deliberately requires four complete words and rejects shorter frames.
- 0x03 has a 3-word threshold but the optimized handler consumes the first two words.
- 0x20 has a 3-word threshold but the optimized handler consumes only the first word.
- 0x12 has a 2-word threshold although the optimized handler's state update uses the first word; the second word's purpose remains unresolved.

0x56 and 0xA3 are additional callback branches but are not entries in the 12-pair table. Their optimized callback paths read the first word directly; the host parser requires one complete word to avoid a short-buffer read.

### Allocation-size audit item

dsp_probe() creates cmd_cachep with a 256-byte object size. cmd_ioctl() can copy up to 256 payload bytes after first writing the 4-byte request header into that object. At the maximum encoded size, the nominal layout is therefore 4 + 256 bytes in a 256-byte object. The module imports __check_object_size(), and the disassembly shows it is called before arm_copy_from_user(). In upstream Linux 4.9.117, __check_object_size() checks heap-object bounds and report_usercopy() calls BUG() on an invalid range ([usercopy.c](https://github.com/gregkh/linux/blob/v4.9.117/mm/usercopy.c)). If the vendor kernel retains this implementation, any size field of 64 words or more reaches a 256-byte copy at object offset +4 and can trigger a kernel BUG. The largest payload that fits the 256-byte object after its header is 252 bytes (63 words). The exact vendor implementation/config still needs confirmation, so this is a high-confidence static crash hazard, not a device-tested result. The host emulator now rejects payloads above 252 bytes with UnsafePayloadSize rather than reproducing the risky request.

## 3. Read-direction ABI

For direction 2, the recovered cmd_ioctl() path accepts only ID 0xF0. It checks that the userspace destination can hold 8 bytes and copies exactly 8 bytes from the beginning of the module's .rodata section.

The first eight .rodata bytes are:

63 64 33 33 31 33 00 00 = "cd3313\0\0".

Thus this read request returns a static cd3313 identifier string, not a live DSP status structure or a hardware version readback. Other read IDs take the error branch. The native caller's read request is recovered exactly as `0x8080F000` (direction READ, type `0xF0`, request number `0`, encoded size `0x80`). The driver still copies only the fixed 8-byte identifier response.

The same ID 0xF0 in write direction is a different callback branch. No state-changing operation was recovered there: a zero-word payload reaches the driver's length-error logger, while a nonzero-size object is freed/dropped. Do not use write-direction 0xF0 as a substitute for the read-direction identifier query.


## 4. Event-queue API declarations recovered from DWARF

The module's DWARF contains external declarations for the imported queue API. The raw DWARF line table resolves the declaration file to the vendor build-tree path:

/work2/publisher26/publish_work/MT8163/kernel-4.9/include/linux/handler_thread.h

The header itself and its implementation are not present in the current repository artifacts. The declarations recovered from DWARF are:

- post_event_delayed(handler*, u32 event_id, void* data, u32 delay_ms) — returns an integer.
- remove_event(handler*, u32 event_id) — returns an integer.
- handler_create(handler_thread*, callback) — returns an opaque handler pointer.
- The callback type is int callback(u32 event_id, void* data).
- handler_thread_create(...) and the matching destroy functions manage the opaque worker-thread object.

handler_struct and handler_thread_struct are only forward declarations in this module's DWARF. The imported helper implementations and their internal queue data structures are absent from cd3313.ko. The module's symbol table lists post_event_delayed and remove_event as undefined imports. Consequently, this module proves call arguments and call order, but not queue cancellation, replacement, coalescing, FIFO ordering, thread scheduling, or callback timing. Return values are not checked at the observed call sites.

## 5. Reconstructed event IDs and scheduling requests

The callback's first argument is an internal event ID; the second is the opaque event-data pointer.

| Event ID | Confirmed callback role |
|---:|---|
| 0 | Register initialization sequence |
| 1 | Dispatch a queued ioctl command buffer |
| 2 | Clear master-mute reason 30 |
| 3 | Clear master-mute reason 24 |

Observed scheduling requests, in call order:

| Context | Requested operations |
|---|---|
| Successful probe | Post event 0, null data, delay 0 ms |
| Write ioctl | Post event 1, pointer to header+payload object, delay 0 ms |
| Mute command 0x11, mute=1, reason=24 | Remove event 3; post event 3 with null data after 1000 ms |
| Wakeup | Remove event 0; post event 0 after 50 ms; remove event 2; post event 2 after 1200 ms; post event 2 again after 1500 ms |
| Shutdown | Remove event 0; remove event 1 |

The second delayed event-2 post during wakeup is not preceded by another remove_event(2) in the observed sequence. Do not assume that both posts remain queued or that one replaces the other: that depends on the unavailable helper implementation.

dsp_remove() calls handler_destroy() and handler_thread_destroy() during teardown. dsp_shutdown() removes event IDs 0 and 1 before its final I²C byte write of 0x3F.

## 6. Confirmed conclusions versus unresolved details

**Confirmed statically**
- Request-word bit positions and direction values.
- Size field is multiplied by four and capped at 256 copied bytes on writes.
- The request word is prepended to the copied payload.
- Write commands are posted as event ID 1 with zero delay.
- Read ID 0xF0 returns the static eight-byte cd3313 identifier.
- Event IDs 0..3 and the scheduling calls listed above.
- The queue API prototypes and callback parameter types recorded in DWARF.

**Still unresolved**
- Exact userspace ioctl macro values, especially the low _IOC_NR byte.
- Internal implementation and semantics of the imported event-queue helpers.
- Whether delayed event posts are coalesced, replaced, or independently retained.
- Exact runtime callback timing and worker scheduling behavior.
- Exact target-kernel hardened-usercopy behavior for the 256-byte cache-object size mismatch.
- Physical meaning of GPIO polarity and hardware response.

No real /dev/audiomux calls, I²C writes, GPIO operations, or device-side experiments are performed by the host emulator.


## Java/Binder caller correlation (new VDEX evidence)

The files2 archive's boot-carservices.vdex now supplies the high-level caller
chain: android.audio.AudioEffect → ISourceService →
SourceService.doExtAudioEffect() → native
ExtAudioMuxer.SendCmdAudioMux(int, int[]) → /dev/audiomux.

The native method's first Java argument is the command ID and the second is
an integer array. For example, setBalance(FL,FR,RL,RR) sends command 0x15
with four 32-bit values, matching the driver's four speaker arguments.
The Java command ID therefore correlates with the driver's bits 8..15 field.
The Binder service special-cases command 1000 as a system-property update;
ordinary CD3313 commands go through the native muxer.

The native transport is now recovered from
`/system/lib/libcarservices_jni.so` in the expanded firmware-library-analysis
artifact. The 32-bit ARM ELF has Build ID
`bf391e545edd2e482b72cd8a431d1c59` and SHA-256
`ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f`.
It registers `android/sourceservice/ExtAudioMuxer` and the native methods
`nativeInit` and `SendCmdAudioMux`. It imports `ioctl`, `__open_2`, and
`close`.

`nativeInit()` opens `/dev/audiomux` read-only, issues
`ioctl(fd, 0x8080F000, zeroed_buffer)`, and closes the descriptor. The
CD3313 read/type-`0xF0` branch copies the eight bytes
`63 64 33 33 31 33 00 00` (`cd3313\\0\\0`) to that buffer. The ioctl
return status is logged separately from the returned chip identifier.

`SendCmdAudioMux(command, values)` obtains the JNI integer-array elements
and array length, opens `/dev/audiomux` write-only, issues ioctl, closes the
descriptor, and releases the elements with `JNI_ABORT`. The exact request
formula from ARM disassembly is:

```c
request = 0x40000000u
        | ((array_length + 1u) << 16)
        | ((command & 0xffu) << 8);
ioctl(fd, request, int_array_elements);
```

Thus direction is WRITE, the command ID occupies bits 8..15, the low
`_IOC_NR` byte is zero, and the size field is `array_length + 1` words.
The kernel multiplies this field by four and copies that many bytes from the
JNI array pointer. For example, Java command `0x15` has four array values,
but the native request asks the kernel to copy five words (20 bytes). The
CD3313 callback consumes only the four documented speaker indices at
payload offsets +4, +8, +12, and +16 in its queued frame; the fifth copied
word is not consumed by that handler. The native/kernel pair therefore has
a statically confirmed one-word over-read discrepancy. Its extra word value
is unspecified; do not guess a padding value or reproduce the unsafe copy
in a new userspace client.

This confirms the final native transport hop to the same `/dev/audiomux`
misc device created by `cd3313.ko`. It remains static firmware analysis:
no live ioctl, I²C transaction, audio change, or hardware test was performed.
See [CD3313_HIDDEN_AUDIOEFFECT_API.md](CD3313_HIDDEN_AUDIOEFFECT_API.md)
for the binary-level evidence and request examples.
