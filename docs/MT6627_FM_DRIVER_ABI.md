# MT6627 FM driver ABI and HAL correlation

## Exact paired kernel module

The firmware contains `vendor_lib/lib/modules/fmradio_drv.ko`:

- Build ID: `f72539936358601db1fe2f208c10d091af454968`
- SHA-256: `17e03615cee4850bd65fa5a69074f767e7f33821cc99a0ff8e40d71b5ddfdaba`
- ARM ELF32 relocatable kernel module.
- Its symbol table contains `mt_fm_probe`, `fm_ops_ioctl`, `fm_get_aud_info`, `fm_i2s_set`, `fm_low_ops_register`, and MT6627-specific callbacks.

### Device-node ownership

`mt_fm_probe()` creates the class and device with the name `fm`, then creates a proc entry with the same name. The class/device creation path establishes that this module owns the `/dev/fm` character device opened by `WCNChipController::InitAudioFMInfo()`. The HAL's `/dev/fm` and the CD3313 module's separate `/dev/fmradio` are different interfaces.

## 1. ioctl 0xC004F530 — five-field FM audio metadata

The HAL calls ioctl `0xC004F530` from `WCNChipController::InitAudioFMInfo()`. The paired kernel module's `fm_ops_ioctl()` has an exact command comparison for `0xC004F530`; that branch calls `fm_get_aud_info()` and copies **20 bytes** to userspace.

`fm_get_aud_info()` calls the registered callback at the FM core structure offset `0xc0`. If the callback is absent, it returns the five fallback values `[3, 2, 2, 3, 2]`, which are error/default enum values rather than a usable board configuration.

The MT6627 callback `mt6627fm_get_audio_info()` is installed by `fm_low_ops_register()`. It copies five consecutive 32-bit values from `fm_config` offsets `0x48, 0x4c, 0x50, 0x54, 0x58` into the output buffer. The driver's diagnostic format names those fields, in order, as:

1. `aud_path`
2. `I2S state`
3. `I2S mode`
4. `I2S rate`
5. `I2S pad`

The HAL's metadata buffer begins at object offset `this+12`; consequently the five fields are read at object offsets `12, 16, 20, 24, 28`. In particular, `aud_path` is at object offset 12, I2S mode at 20, rate at 24, and pad at 28.

**ABI quirk:** the ioctl value encodes `_IOC_SIZE=4`, but the kernel copies 20 bytes. The HAL allocates/passes the full five-word structure. Do not size the buffer from the ioctl number's encoded size.

## 2. ioctl 0xC004F521 — I2S setting

The HAL's `WCNChipController::SetFmChipSampleRate(unsigned int)` issues ioctl `0xC004F521`. It builds a three-word payload:

`[0, current_i2s_mode, sample_rate_enum]`

The sample-rate enum is independently confirmed in the HAL:

| Enum | Rate |
|---:|---:|
| 0 | 32,000 Hz |
| 1 | 44,100 Hz |
| 2 | 48,000 Hz |
| 3 | Error |

The kernel `fm_ops_ioctl()` branch for `0xC004F521` copies **12 bytes** from userspace and passes the three words to `fm_i2s_set()`. That function validates the first field, then invokes a callback stored in the FM core structure at offset `0x8c`, passing the three values. The identity and board-specific implementation of that setter callback are not yet resolved.

**ABI quirk:** this ioctl also encodes `_IOC_SIZE=4`, while the kernel copies 12 bytes. The HAL supplies all three words.

## 3. ioctl 0xC004F529 — separate I2S-info query

The kernel dispatcher also handles `0xC004F529` through `fm_get_i2s_info()` and copies **12 bytes** back to userspace. Its callback is stored at FM core offset `0x90`; the registered MT6627 callback `mt6627_i2s_info_get()` reads three values from `fm_config` offsets `0x4c, 0x50, 0x54`.

This is a separate three-field I2S-info query. The HAL metadata initialization path documented above uses `0xC004F530` (five fields), not this three-field request.

## 4. HAL interpretation of the metadata

The exact HAL checks already established from `audio.primary.mt8163.so` are:

- `aud_path`: value 0 is `FM_AUD_ANALOG`; value 2 means merge interface supported; value 3 is `FM_AUD_ERR`. Initialization rejects the analog path. Value 1 is a valid non-merge value in the HAL branch logic, but its symbolic enum label is not asserted here.
- I2S mode: 0 = master, 1 = slave, 2 = error.
- I2S sample rate: 0 = 32 kHz, 1 = 44.1 kHz, 2 = 48 kHz, 3 = error.
- I2S pad: 0 = CONN, 1 = IO, 2 = error.

The merge-interface result selects the exact HAL endpoints documented in [MT8163_FM_ROUTING_MATRIX.md](MT8163_FM_ROUTING_MATRIX.md):

| Merge-interface result | Direct playback Card/PCM | AP capture Card/PCM |
|---|---|---|
| true | `MRGRX_PLayback` | `MRGRX_CAPTURE` |
| false | `FM_I2S_Playback` | `FM_I2S_Capture` |

## 5. WMT platform I2S pin control

The paired firmware also contains `wmt_drv.ko` (Build ID `c2e5f3477920471011b2d929be212100b0d41ff8`; SHA-256 `90d31e43f4883f9e3cf707cc0ae351672afa2c57ab98d7c866b7efd0d1fb1e8b`). Its `wmt_plat_i2s_ctrl()` routine checks platform `gpio_ctrl_info` fields at offsets `0x240`, `0x26c`, and `0x298`; missing values trigger diagnostics for I2S CK, I2S WS, and data-clock pins. The module also contains diagnostics stating that host I2S pins may be undefined and that when FM digital mode is not set, I2S GPIOs need not be configured.

The WMT library initialization registers `wmt_lib_set_aif()` through `wmt_plat_aif_cb_reg()` at callsite `0x20f24`. The `wmt_plat_i2s_ctrl()` function is an entry in the platform GPIO-control dispatch table beside `wmt_plat_pcm_ctrl()`. This confirms a separate WMT audio-interface and pin-control layer. It does not, by itself, prove the exact physical FM pin assignment or establish the complete callback path from the MT6627 setter to the board pinmux.

The focused inventory of the supplied firmware tree found no `.dts`, `.dtsi`, `.dtb`, `.dtbo`, boot image, or preloader image. The actual I2S pad numbers and GPIO pinmux therefore remain unavailable from the current dump.

## 6. Confirmed versus unresolved

**Confirmed from paired binaries**

- `fmradio_drv.ko` owns `/dev/fm`.
- `0xC004F530` returns the five-word audio metadata structure (20 bytes).
- `0xC004F521` accepts the three-word I2S setting structure (12 bytes).
- `0xC004F529` is a separate three-word I2S-info query.
- MT6627 supplies the five metadata fields from `fm_config`.
- HAL endpoint names and merge-mode branches are resolved.

**Still open**

- The concrete implementation registered at the FM core's I2S-set callback offset `0x8c`.
- The runtime values of `fm_config` on the target board.
- The selected Audio Policy XML at runtime.
- Numeric ALSA card/PCM indexes discovered by AudioALSADeviceParser.
- Physical pinmux and board-level wiring from the tuner to CD3313 Source 3 and/or AP I2S.

No real-device ioctl writes, I²C writes, GPIO changes, or route changes were performed.


## Module dependency and runtime-load qualification

The paired `fmradio_drv.ko` contains the module dependency string `depends=wmt_drv`, consistent with its use of the WMT connectivity/platform layer. The supplied init/config text scan found comments in both the vendor audio-HAL service RC and system audioserver RC stating that the `media` group is needed for `/dev/fm`. However, the scan found no explicit `insmod`, `modprobe`, or `modules.load` entry for `fmradio_drv.ko` or the alternative tuner modules in the extracted init/config set. The presence of the module and its exact matching ioctl ABI identifies the intended provider, but does not prove that this module is loaded on a live target. The other tuner modules (`qn8027.ko`, `qn8035.ko`, `si475x.ko`, `tda7708LX.ko`) are also present; their presence alone does not establish that they are selected or active.
