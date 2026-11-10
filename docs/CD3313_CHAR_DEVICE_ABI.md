# CD3313 character-device read/write ABI

## Evidence and scope

Recovered from the ARM32 cd3313.ko artifact in focused analysis workflow run [42883809539](https://github.com/14march93/mt8163_audio_engine/actions/runs/42883809539).

- Module SHA-256: b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb
- Misc-device name string: audiomux
- Device path convention: /dev/audiomux
- The read/write paths are static disassembly findings. No live device-node access has been performed.

### Independent MT8163 device-tree corroboration

A separate public MT8163 device tree also loads cd3313.ko and configures /dev/audiomux as mode 0664, owner root, group system:
- [init.mt8163.rc](https://github.com/Tec4Sho/android_device_alps_tb8163p3_bsp/blob/dae4caeb433746ee84e42443ea1b904f3b590a59/recovery/root/init.mt8163.rc)
- [ueventd.rc](https://github.com/Tec4Sho/android_device_alps_tb8163p3_bsp/blob/dae4caeb433746ee84e42443ea1b904f3b590a59/recovery/root/ueventd.rc)

This corroborates the device-node name and a plausible system-group permission model on another MT8163 board. It is not proof of the permissions or init sequence on the user's exact head unit.


The character device exposes three distinct interfaces: read(), write(), and ioctl(). The ioctl request framing is documented separately in [CD3313_IOCTL_ABI.md](CD3313_IOCTL_ABI.md).

## 1. read(): formatted software-state report

cmd_read() builds a text report in a 1024-byte stack buffer using sprintf(), then returns it through simple_read_from_buffer(). It does not issue an I²C read transaction. The register values shown are the driver's cached reg_bufs bytes, not verified hardware readback.

The report contains these sections:

- Header: “Chip CD3313” and “Read reg Data:”.
- Eight cached register bytes, in reg_bufs slot order:
  1. master volume
  2. RL volume
  3. RR volume
  4. FL volume
  5. FR volume
  6. channel control
  7. bass gain
  8. treble gain
- Current master channel name and numeric channel ID.
- Master volume step, master-mute reason mask, mixing state, HFP activity, ACC activity, and loudness state.
- Seven per-channel rows containing channel name, numeric ID, mute-reason mask, and input-gain value.

The exact format strings recovered from .rodata.str1.4 are:

    Chip CD3313
    Read reg Data:
    index 00xx xxxx: 0x%02x (master volume)
    index 110x xxxx: 0x%02x (RL volume)
    index 111x xxxx: 0x%02x (RR volume)
    index 100x xxxx: 0x%02x (FL volume)
    index 101x xxxx: 0x%02x (FR volume)
    index 010x xxxx: 0x%02x (channel)
    index 0110 xxxx: 0x%02x (bass gain)
    index 0111 xxxx: 0x%02x (treble gain)
    master channel : %s(%d)
    master volume   : %d
    master mute     : 0x%x
    mixing state    : %d
    hfp activity    : %d
    acc activity    : %d
    loudness on     : %d
    channel status:
    channel %6s(%d) : mute:%x, gain %d

The seven driver channel labels are recovered from a pointer table in .rodata:

| Channel ID | Driver label |
|---:|---|
| 0 | RADIO |
| 1 | ARM |
| 2 | AUX |
| 3 | EXBT |
| 4 | AUX2 |
| 5 | DAB |
| 6 | NONE |

These labels are explicit strings in the driver. They do not, by themselves, prove the physical wiring or the exact audio route for each source.

The report is a software snapshot assembled from global state and cached bytes. Because the function builds it anew for each read call, a multi-call read using file offsets could theoretically span changing state. No synchronization around the snapshot was identified in the recovered function.

This file read() operation is distinct from the ioctl read direction. ioctl ID 0xF0 returns exactly eight static bytes, cd3313 followed by two NUL bytes; it does not return this status report. See [CD3313_IOCTL_ABI.md](CD3313_IOCTL_ABI.md).


## 2. write(): raw one-byte I²C command path

cmd_write() is separate from the high-level ioctl dispatcher.

1. It zeroes a 12-byte local text buffer.
2. It copies at most 12 bytes from the userspace buffer with simple_write_to_buffer().
3. It parses the text as an unsigned integer using kstrtouint() with base 16.
4. It takes only the low 8 bits of the parsed integer.
5. It calls i2c_master_send() with exactly one byte.
6. It maps the command byte's high nibble through TAB.33841 and updates the corresponding reg_bufs cache slot.

Important observed behavior:

- This is a hexadecimal text interface, not a binary command-frame interface.
- There is no explicit check that the parsed integer fits in 8 bits; the low byte is used.
- The i2c_master_send() return value is ignored.
- The cache is updated after the send call without checking whether the I²C transfer succeeded. A failed transfer can therefore leave the software cache inconsistent with the chip.
- The function returns the original userspace count even when hex parsing fails. It also copies no more than 12 bytes even if the caller supplied a longer buffer.

### Static string-termination audit

The function zeroes exactly 12 bytes and gives simple_write_to_buffer() an available size of 12, then passes that buffer to kstrtouint(). The Linux 4.9.117 implementation of simple_write_to_buffer() copies up to all 12 available bytes and does not append a NUL terminator; kstrtouint() documents that its input must be NUL-terminated. The disassembly places the stack protector immediately after this 12-byte buffer. Therefore a full 12-byte input consisting of valid hexadecimal digits can make the parser inspect beyond the local buffer while searching for its terminator. This is a static one-byte-or-more stack-read hazard, not evidence of a write or a demonstrated exploitable leak. The vendor kernel's exact helper implementations should be checked before making a security-impact claim.

References: [Linux 4.9.117 simple_write_to_buffer()](https://github.com/gregkh/linux/blob/v4.9.117/fs/libfs.c) and [Linux 4.9.117 kstrtouint()/kstrtoull()](https://github.com/gregkh/linux/blob/v4.9.117/lib/kstrtox.c).

- The raw write path does not run the high-level command dispatcher or its argument validation.

The function passes the VFS file-position pointer directly to simple_write_to_buffer() and does not reset it. After the first write, the file position advances. On a later write using the same file descriptor, simple_write_to_buffer() places the new text at a nonzero offset in a freshly zeroed buffer; kstrtouint() then sees a NUL at byte 0 and rejects the empty string. The driver still returns the original count. Therefore ordinary repeated write() calls on one open descriptor appear to process only the first command; reopening the device (or explicitly writing at offset 0) is required for another command. This is a static consequence of the disassembly plus the Linux 4.9.117 helper implementation, not a runtime-tested behavior.


The emulator's write_raw_command_byte() models the one-byte command/cache behavior. It does not yet model the permissive text parser or the driver's error-reporting quirks.

## 3. GPIOs and board-control labels recovered from probe

dsp_probe() requests these GPIOs by name, configures them as raw outputs, and writes their initial raw values:

| GPIO | Requested name | Initial raw value |
|---:|---|---:|
| 393 | mute | 1 |
| 409 | subwoof | 1 |
| 396 | auxsw | 0 |
| 398 | dabradiosw | 0 |

Further call-site evidence:

- GPIO 396 (auxsw) is driven raw-low for driver channel 2 (AUX) and raw-high for other channels.
- GPIO 398 (dabradiosw) is driven raw-low for driver channel 0 (RADIO) and raw-high for other channels.
- Command 0xA3 reads its first payload word and drives GPIO 409 raw-low for nonzero input, raw-high for zero input.
- GPIO 393 is the master mute output and follows the separate mute/ACC/HFP/reason-mask logic documented in the command-dispatch notes.

The labels and raw levels are confirmed. The electrical polarity and downstream board behavior still require the schematic or physical measurement.

set_input_channel() also probes /dev/fmradio with filp_open() when the internal_radio state is negative, then updates that state based on whether the open succeeded. The exact higher-level meaning of this probe is not established.

## 4. Channel-to-chip-input correlation

The driver channel labels can now be combined with CSWTCH.194 and the manufacturer datasheet:

| Driver channel | Label | Selector code | CD3313 input |
|---:|---|---:|---|
| 0 | RADIO | 2 | Input 3 |
| 1 | ARM | 1 | Input 2 |
| 2 | AUX | 0 | Input 1 |
| 3 | EXBT | 3 | Input 4 (no external pins) |
| 4 | AUX2 | 0 | Input 1 |
| 5 | DAB | 2 | Input 3 |
| 6 | NONE | 3 | Input 4 (no external pins) |

The CD3313 datasheet describes three external stereo inputs; input 4 has no external pins. Therefore EXBT and NONE both select the no-pin input code in this driver. Do not assume that EXBT is physically connected to the CD3313 analog input path; it may be a separate route, but that requires board-level evidence.

## 5. Remaining work

- Recover the matching system framework implementation of android.audio.AudioEffect used by HEQ; it may be the high-level caller for the ioctl interface.
- Reconstruct exact userspace ioctl macro values and call ordering.
- Verify the raw write parser's behavior on the target kernel and whether the usercopy helper constrains all malformed inputs.
- Compare the software report with a live read-only device snapshot when hardware becomes available.
- Verify GPIO polarity and the board-level routing for RADIO, AUX, DAB, EXBT, and subwoofer.

No live I²C writes, GPIO changes, or device-node operations are performed by this host-side analysis.
