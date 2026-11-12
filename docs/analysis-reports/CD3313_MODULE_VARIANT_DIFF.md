# CD3313 kernel-module variant comparison

Date: 2026-11-12  
Static comparison of the two imported files:

- Standalone reference: `firmware/dumps/mt8163/kernel/cd3313.ko`
- Vendor archive member: `vendor_lib/lib/modules/cd3313.ko`

## 1. Build identity

| Property | Standalone reference | vendor_lib archive |
|---|---|---|
| Size | 183,368 bytes | 188,520 bytes |
| SHA-256 | `b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb` | `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594` |
| Build ID | `dae15c9eb019e9388d15cb88088c202d017ebcf0` | `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56` |
| Compiler | GCC 4.9.x 20150123 | GCC 4.9.x 20150123 |
| vermagic | `4.9.117+ SMP preempt mod_unload modversions ARMv7 p2v8` | same |

The embedded debug/source paths identify the standalone build as `publisher26` and the vendor archive build as `publisher28`. The supplied `build.prop` fingerprint is also a `publisher28` build. More importantly, `init.mt8163.rc` loads `/vendor/lib/modules/cd3313.ko`, which maps to the vendor archive member, not the standalone reference path.

**Conclusion:** the vendor archive module is the stronger candidate for the module actually installed by this firmware. The standalone module is an older/different reference build and must not be used as a proxy for current device behavior.

## 2. Function-level differences

| Function | Standalone | vendor_lib | Observation |
|---|---:|---:|---|
| `cmd_ioctl` | 452 bytes | 452 bytes | Same instruction count; two instruction-word differences are warning line/relocation details |
| `cmd_read` | 692 bytes | 692 bytes | Instruction words identical |
| `set_master_mute` | 144 bytes | 144 bytes | Instruction words identical |
| `cmd_write` | 184 bytes | 272 bytes | Vendor adds transfer-result checking and retry logic |
| `cd3313_write_reg` | 68 bytes | 164 bytes | Vendor adds transfer-result checking and retry logic |
| `set_input_channel` | 840 bytes | 1,012 bytes | Expanded implementation |
| `set_master_volume` | 380 bytes | 512 bytes | Expanded implementation |
| `event_callback` | 2,416 bytes | 2,568 bytes | Expanded implementation |
| `dsp_shutdown` | 248 bytes | 332 bytes | Expanded implementation |
| `set_equalizer_bassboost` | absent | 280 bytes | Additional function in vendor build |

The vendor build also contains additional symbols/strings including `STATUS_WRITE_CMD_MASTER_VOLUME_MAX`, `volume_max`, `treble`, `balance`, `boost` and `retry`.

## 3. Confirmed I2C write-handling improvement

### Older standalone reference

The standalone `cd3313_write_reg` performs one `i2c_master_send`, then updates the cached register value. It does not require the transfer result to equal the expected one-byte length before updating the cache; its return path only checks the sign bit. A short transfer can therefore be treated as success and leave the cache inconsistent with the chip.

The standalone raw `cmd_write` path also sends once and updates the cache without checking that the one-byte transfer completed.

### vendor_lib build

The vendor `cd3313_write_reg`:

1. checks the I2C transfer result against exactly one byte;
2. updates the cached register value only on that successful result;
3. waits 20 ms and logs the transfer result on failure;
4. retries up to five attempts before returning an error.

The vendor raw `cmd_write` path also checks for an exact one-byte transfer, retries up to five times with 20 ms delays, and updates the cache only after success.

**This is a confirmed correction in the vendor build** relative to the older standalone reference. The earlier static concern about unconditional cache updates applies to the standalone reference, not to the vendor archive version of these two write paths.

## 4. What remains to inspect

- The two `cmd_ioctl` functions have the same size and instruction count, with only two instruction-word differences, but the full ABI/payload validation still needs review.
- Compare `set_input_channel`, `set_master_volume`, `event_callback` and `dsp_shutdown` semantically; their code grew and the vendor build adds volume/EQ functionality.
- Reconcile the vendor module's command table with the JNI `SendCmdAudioMux(int, int[])` caller and AudioFlinger/AudioPolicy mute clients.
- Confirm which DSP driver actually binds to the board's I2C client using DT/DTBO and runtime probe evidence.

## Scope

This is offline ELF/debug-symbol/disassembly analysis. No kernel module was loaded, no live device was accessed, and no I2C/GPIO state was changed.
