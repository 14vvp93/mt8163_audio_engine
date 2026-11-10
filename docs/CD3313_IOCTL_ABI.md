# CD3313 ioctl ABI and event-queue reconstruction

## Evidence identity and scope

This document is based on the ARM32 cd3313.ko analysis artifact produced by focused workflow run [42878596077](https://github.com/14march93/mt8163_audio_engine/actions/runs/42878596077).

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

The command ID is taken from the Linux _IOC_TYPE position, not the low _IOC_NR byte. The driver does not inspect bits 0..7 in the recovered cmd_ioctl() path. The exact userspace macro/magic convention and the values placed in those low bits remain unverified.

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

dsp_probe() creates cmd_cachep with a 256-byte object size. cmd_ioctl() can copy up to 256 payload bytes after first writing the 4-byte request header into that object. At the maximum encoded size, the nominal layout is therefore 4 + 256 bytes in a 256-byte object. The disassembly also shows a call to __check_object_size() before arm_copy_from_user(). This is a real static size-geometry mismatch worth auditing against the exact vendor kernel's hardened-usercopy implementation; this artifact alone does not prove an exploitable overwrite or establish how that check behaves in the target build. Normal reconstructed commands use much smaller payloads.

## 3. Read-direction ABI

For direction 2, the recovered cmd_ioctl() path accepts only ID 0xF0. It checks that the userspace destination can hold 8 bytes and copies exactly 8 bytes from the beginning of the module's .rodata section.

The first eight .rodata bytes are:

63 64 33 33 31 33 00 00 = "cd3313\0\0".

Thus this read request returns a static cd3313 identifier string, not a live DSP status structure or a hardware version readback. Other read IDs take the error branch. The read path's exact userspace ioctl macro and low-byte _IOC_NR value are not recovered.

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
