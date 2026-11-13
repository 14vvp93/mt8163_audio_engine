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
