# MT8163 boot kernel, embedded config and DTBO audit

Date: 2026-11-13  
Repository evidence source: `14vvp93/public_calculations`  
Firmware analysis run: [43238133570](https://github.com/14vvp93/public_calculations/actions/runs/43238133570)

## 1. Evidence provenance

This audit uses the firmware-image-analysis artifact (artifact ID `13951994837`) and board-audio-routing artifact (artifact ID `13953597221`) from the run above. The artifacts were downloaded from GitHub Actions and inspected locally. Hashes below identify the exact extracted objects.

| Object | SHA-256 |
|---|---|
| Source `boot.img` (worker report) | `1ce6ddb5188b731f35900e182131a489974e61a2df4b67f46602b64e2504f422` |
| Extracted boot zImage | `9b0740958d6e75597ef87a67219b49f32b8c6c90983a200baeae8f654eb4dbd7` |
| Extracted boot kernel config | `e35b2914584c85dffe3d3585d42fbce6565e57d87d191a0e2480e6dfdd3226d1` |
| Source `dtbo.img` (worker report) | `e65d8aec97920ce01a8c46118757e5fb629cdc4e5d209252f1ecc6308ceb15f1` |
| Extracted DTBO entry | `f2bb637e0656c8db88f3fa4a815f8b650bc59deed52502a140f93c99c74b200c` |

## 2. Boot image and kernel identity

The worker's Android boot-image parser reports:

- Android boot magic `ANDROID!`, header version 1, page size 2048.
- Android OS version 9.0.0; image header patch level 2021-09.
- Kernel size 7,054,702 bytes; load address `0x40008000`.
- Ramdisk size 376,988 bytes; load address `0x55000000`.
- Kernel is an ARM zImage. Its embedded gzip payload was decompressed for inspection.

The decompressed kernel identifies itself as:

```text
Linux version 4.9.117+ (publisher28@ubuntu128)
(gcc version 4.9.x 20150123 (prerelease) (GCC))
#1 SMP PREEMPT Thu Mar 26 17:29:53 CST 2026
```

This is direct evidence for the firmware's kernel release/build identity. It is not the kernel source tree and does not by itself establish the exact build toolchain package revision.

## 3. Embedded kernel configuration

The kernel has `CONFIG_IKCONFIG=y` and `CONFIG_IKCONFIG_PROC=y`. The embedded configuration was extracted from the decompressed kernel's `IKCFG_ST` / `IKCFG_ED` section. Boot and recovery configs are byte-identical (SHA-256 above).

Relevant exact settings:

| Setting | Value |
|---|---|
| `CONFIG_ARCH_MEDIATEK` | `y` |
| `CONFIG_MACH_MT8163` | `y` |
| `CONFIG_ARM` | `y` |
| `CONFIG_SMP` | `y` |
| `CONFIG_PREEMPT` | `y` |
| `CONFIG_MODULES` | `y` |
| `CONFIG_MODVERSIONS` | `y` |
| `CONFIG_IKCONFIG` / `CONFIG_IKCONFIG_PROC` | `y` / `y` |
| `CONFIG_HARDENED_USERCOPY` | `y` |
| `CONFIG_HARDENED_USERCOPY_PAGESPAN` | not set |
| `CONFIG_SLUB` | `y` |
| `CONFIG_CC_STACKPROTECTOR_STRONG` | `y` |
| `CONFIG_DEBUG_INFO` | `y` |
| `CONFIG_SND_SOC_MT8163` | `y` |
| `CONFIG_ANDROID_BINDER_IPC` | `y` |
| `CONFIG_DEBUG_PAGEALLOC`, `CONFIG_DEBUG_VM`, `CONFIG_UBSAN` | not set |

The decompressed kernel contains the `__check_object_size` string and the hardened-usercopy diagnostic string. The configuration proves that hardened usercopy is enabled in this build. The binary inspection performed here does not replace source-level review of the exact implementation.

### Security consequence for the legacy JNI over-read

`CONFIG_HARDENED_USERCOPY=y` must not be interpreted as protection against the known Java `int[]` logical-boundary over-read. The kernel receives a userspace pointer, not Java array metadata. Hardened usercopy checks kernel-side copy-object bounds; `copy_from_user()` can reject inaccessible user pages, but it cannot generally know the declared length of the Java array. If the extra word remains in a readable userspace page, the logical over-read can still occur.

Therefore the JNI framing defect remains real and must be corrected in the replacement bridge with an explicitly sized, fully initialized buffer. Do not waive the P0-A fix because hardened usercopy is enabled.

## 4. Device-tree overlay evidence

The separate `dtbo.img` contains one overlay entry. The extracted DTB is version 17 and 42,594 bytes. Its decompiled report includes:

```dts
cd3313@44 {
    compatible = "hcn,cd3313";
    mute-gpios = <... 0x24 0x01>;
    sub-gpios = <... 0x34 0x01>;
};
```

This is board-configuration evidence that the supplied DTBO declares a CD3313-compatible node and mute/sub GPIO properties. The GPIO phandles have not been resolved against a base DTB in this audit. A DTBO declaration does not prove that the IC is physically populated, that the overlay is selected at boot, or that the driver probe succeeds.

## 5. Module-load and FM route evidence

The recovered `init.mt8163.rc` references show this module insertion order in the same init block:

1. `carstatus.ko`
2. `cd3313.ko`
3. `ak7604.ko`
4. `fy7604.ko`
5. FM tuner modules, including `si475x.ko`, `qn8035.ko`, `tda7708LX.ko` and `qn8027.ko`

The three audio modules are all referenced by init despite the previously documented shared `audiomux` misc-device-name collision. This makes actual module insertion/probe logs and the runtime `/dev/audiomux` owner mandatory evidence. The init script alone does not prove that all three modules load successfully or which one owns the device node.

The recovered vendor audio policy declares FM Tuner In as an attached input and routes it to Speaker, Wired Headset and Wired Headphones. It also declares an FM Tuner Out device port. Static strings in the factory primary HAL include `FM_I2S_Playback`, `FM_I2S_Capture`, `MRGRX_PLayback`, `MRGRX_CAPTURE`, `FM_DIRECT_CONTROL`, `FM_IOCTL_I2S_SETTING` and `FM_IOCTL_GET_AUDIO_INFO`.

These symbols establish that the factory HAL contains direct-I2S and capture-related FM code paths. They do not prove which path is active for a particular tuner, board revision or runtime route. Keep FM as a distinct route class until runtime evidence resolves the path.

## 6. P0 status and remaining evidence

### Confirmed by this audit

- Exact firmware kernel release: Linux 4.9.117+.
- Exact embedded kernel config recovered; hardened usercopy is enabled.
- MT8163 SoC configuration is enabled in the kernel.
- A CD3313-compatible node is declared in the supplied DTBO.
- Init references all three audio module variants; runtime owner remains unresolved.
- Factory HAL contains FM I2S/capture/direct-control code paths; active route remains unresolved.

### Still required

- Exact vendor kernel source tree and matching build configuration/toolchain package.
- Source-level review of this kernel's `__check_object_size` and hardened-usercopy implementation.
- Runtime boot/probe logs, loaded-module list, `/dev/audiomux` major/minor and actual module owner.
- Base DTB resolution for the CD3313 GPIO phandles and confirmation that this DTBO is selected.
- Runtime ALSA card/PCM/mixer evidence and proof of FM direct-I2S versus capture behavior.
- Physical confirmation of populated DSP/module hardware.

No ioctl, I2C write, GPIO write, mixer write, module load or physical-device operation was performed.


## 7. User-confirmed provenance for the physical head unit — 2026-11-13

The user confirmed that the firmware dump analyzed in workflow run `43238133570` is from the same physical MT8163 head unit whose populated audio IC is CD3310EO.

This makes the `cd3313@44` / `compatible = "hcn,cd3313"` node a direct firmware declaration for this unit, rather than a declaration from an unrelated reference board. The physical marking and firmware declaration therefore form a confirmed discrepancy:

- Physical IC: CD3310EO (user-confirmed).
- Firmware DTBO node: CD3313-compatible at address `0x44` (static artifact verified).
- Firmware module: `cd3313.ko`, with I2C ID `cd3313` and OF compatible `hcn,cd3313` (static ELF/DWARF verified).

Do not collapse these into a claim that the IC is actually CD3313 or that the CD3313 driver is compatible with CD3310EO. The selected overlay, live device-tree node, successful driver probe, and runtime `/dev/audiomux` owner remain unverified. The next evidence required is the base-DTB/overlay selection path plus read-only runtime module, I2C-device, driver-binding, and device-node ownership data.

No hardware transaction or device operation was performed.


## 8. Embedded base DTB and I2C2 overlay resolution — 2026-11-13

A follow-up binary parse recovered the base FDT that was not included in the earlier DTBO-only summary.

### Exact objects

| Object | Location | Size | SHA-256 |
|---|---:|---:|---|
| Base DTB | `boot.img + 7,014,136` | 42,614 bytes | `b17bff1b3251882864700ca290efd6d0326997356bb98ed10a8dd8d012ae72a0` |
| Base DTB copy | `recovery.img + 7,014,136` | 42,614 bytes | identical to boot DTB |
| Overlay DTB | `dtbo.img + 64` | 42,594 bytes | `f2bb637e0656c8db88f3fa4a815f8b650bc59deed52502a140f93c99c74b200c` |

The base DTB embedded in `boot.img` is byte-identical to the base DTB embedded in `recovery.img`. The overlay DTB in `dtbo.img` is byte-identical to the second FDT appended in `recovery.img`.

The Android DTBO header reports one table entry (header size 32, entry size 32, entry count 1, entry offset 32, page size 2048). The entry points to the 42,594-byte overlay at offset 64. This is one overlay image containing multiple fragments, not one fragment or one audio device.

### I2C controller state and overlay target

In the base DTB, all three MT8163 I2C controllers are initially disabled:

- `/soc/i2c@11007000`, phandle `0x38`
- `/soc/i2c@11008000`, phandle `0x39`
- `/soc/i2c@11009000`, phandle `0x3a`

The overlay `__fixups__` maps symbol `i2c2` to `/fragment@11:target:0`; the base DTB phandle map resolves `i2c2` to `/soc/i2c@11009000`. Fragment 11 sets that controller's status to `okay` and adds multiple I2C child nodes.

Among the children of this same I2C2 overlay fragment are:

| Node | Compatible | Address | Status |
|---|---|---:|---|
| `cd3313@44` | `hcn,cd3313` | `0x44` | `okay` |
| `ak7604@1c` | `hcn,ak7604` | `0x1c` | `okay` |
| `fy7604@50` | `hcn,fy7604` | `0x50` | `okay` |

The same fragment also declares several alternative FM-tuner nodes (SI4745, SI475x, TDA7786, TDA7708LX, TEF6856 and QN8027). This broad set of alternatives indicates that the overlay is multi-device/multi-variant configuration. It must not be interpreted as a physical population list. The user has confirmed that only CD3310EO is physically populated among the external audio processors.

The DTBO table having one entry and the overlay containing these nodes do not establish that the bootloader selected this entry on the running unit. Runtime overlay selection and live I2C client enumeration remain unverified.

## 9. Competing DSP module probe behavior — static comparison

The three vendor modules share the `/dev/audiomux` misc-device name, but their visible probe checks are not equivalent.

- `ak7604.ko` (SHA-256 `be362f5c5bc448f396bffbc77c460d40003bb496deb0311424c4d97bc2d54ba1`) calls `ak7604_check_exist` before `misc_register`. The check uses an I2C transfer with a write/read exchange and validates the returned byte. A zero result takes the probe failure path; `misc_register` is not reached.
- `fy7604.ko` (SHA-256 `3a09acda96db7ab89853c37276d2bef37e3c112262bb854475baa04b31046fa5`) calls `fy7604_check_exist` before `misc_register`. The check performs an I2C read transaction and validates returned data. A zero result takes the probe failure path; `misc_register` is not reached.
- `cd3313.ko` uses the previously documented one-byte `0x3f` write through `i2c_master_send(..., 1)`; a return value of one is sufficient for its visible probe path to continue. No silicon-ID read is visible in that probe function.

The recovered init script requests `cd3313.ko`, then `ak7604.ko`, then `fy7604.ko`. Therefore a plausible sequence is that the CD3313 module attempts to claim `/dev/audiomux` first if a device at address `0x44` acknowledges the one-byte write; the later drivers perform their own existence checks. This is a **static hypothesis**, not a runtime finding. It does not prove that CD3310EO ACKs the byte, that the module loads, or that any CD3313 command is compatible with CD3310EO.

This comparison explains why a CD3313-named module may be present in a CD3310EO unit's firmware without establishing silicon identity. No module was loaded and no I2C operation was issued.


## 10. Shared GPIO ownership and probe ordering — 2026-11-13

A further disassembly pass found a second ordering dependency beyond the shared `/dev/audiomux` name.

- The CD3313 `dsp_probe` first completes its one-byte I2C write check. On success it requests GPIO IDs `0x189` and `0x199` (among other GPIOs), then proceeds toward `misc_register`.
- The AK7604 `dsp_probe` requests `0x189`, `0x182` and `0x199`. It checks each `gpio_request` result and exits its probe on a negative result. Its `ak7604_check_exist` call occurs only after these GPIO requests.
- The FY7604 `dsp_probe` requests the same `0x189`, `0x182` and `0x199` IDs, checks each request result, and calls `fy7604_check_exist` only after the GPIO requests succeed.
- The init script inserts CD3313 first, then AK7604, then FY7604 (lines 563–565 of the extracted `init.mt8163.rc`).

Therefore, if the CD3313 probe succeeds and retains the shared GPIO requests, the later AK7604/FY7604 probes can fail at GPIO acquisition before their chip-existence checks. Combined with the shared misc-device name, this creates a strong static explanation for why CD3313 is the first candidate to own the audio-mux endpoint on this firmware.

This is still conditional: the CD3313 probe must first receive a successful one-byte I2C transfer at its configured address, and runtime module/probe logs are unavailable. The disassembly does not prove that the physical CD3310EO acknowledged the write or that CD3313 register commands work on it. Do not attempt to verify this with a live I2C write.
