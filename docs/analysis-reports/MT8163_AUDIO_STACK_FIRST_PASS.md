# MT8163 audio stack — first-pass findings

Date: 2026-11-12  
Source repository: `14march93/mt8163_audio_engine`  
Source commit imported: `44e132c8ac299ebfd58831232ec6b6f64ae8f769`  
Extraction workflow: [run 43205114889](https://github.com/14vvp93/public_calculations/actions/runs/43205114889)  
Audio inventory workflow: [run 43206552330](https://github.com/14vvp93/public_calculations/actions/runs/43206552330)  
Latest analysis artifact: `public-compute-extract-43206552330` (14-day retention; expires 2026-11-26)

## 1. Extraction result

All four split archive sets were reassembled, CRC-checked and extracted successfully. The 26 source volumes were inventoried as split-ZIP volumes and retained unchanged.

| Archive set | Files | Expanded bytes |
|---|---:|---:|
| `files2.zip` | 324 | 128,875,776 |
| `HEQ.zip` | 1 | 168,811,733 |
| `lib.zip` | 766 | 184,791,172 |
| `vendor_lib.zip` | 325 | 104,706,028 |
| **Total** | **1,416** | **587,184,709** |

No archive-set extraction errors were reported. The artifact contains the per-member SHA-256 manifest, archive-volume inventory, Markdown summary and extracted tree. The artifact is temporary; the original input volumes remain in the repository.

## 2. Confirmed Android audio architecture

### Android-facing HAL and PCM path

- `android.hardware.audio@4.0-service-mediatek` is configured as the vendor HIDL Audio HAL service. Its init service runs as `audioserver` and advertises `android.hardware.audio@4.0::IDevicesFactory/default`.
- `android.hardware.audio@4.0-impl-mediatek.so` is a 32-bit ARM / Android 28 HIDL implementation and depends on `vendor.mediatek.hardware.audio@4.1.so` plus Android HIDL/audio libraries.
- `audio.primary.mt8163.so` is a 32-bit ARM / Android 28 shared object. Its dependency set includes TinyALSA/TinyCompress, ALSA utilities, MediaTek resampling and audio component libraries, audio settings, DCR filter and audio toolkit components.
- The primary HAL contains PCM and mixer-control paths (including TinyALSA calls and controls such as speaker/headset amplifiers, ADC switches and sidetone). This is the main PCM/mixer integration surface to map before replacing vendor behavior.

### External DSP / audio-mux path

Static strings and symbols establish an additional control path outside the primary HAL:

- `libaudioflinger.so` references `/dev/audiomux` and `/dev/carstatus`; it contains the diagnostic string `setAudioOutputMute Mute:%d, carstatus fd %d, audiomux fd %d`.
- `libaudiopolicymanagerdefault.so` also references both device nodes and has a separate `setAudioOutputMuteTimeout` path.
- `libcarservices_jni.so` registers `android/sourceservice/ExtAudioMuxer`, exposes JNI method `SendCmdAudioMux(I[I)I`, reads property `ro.hw.dsp.chip`, and references `/dev/audiomux`.
- The kernel modules `cd3313.ko`, `ak7604.ko` and `fy7604.ko` each contain an `audiomux` misc-device registration path and a registration-failure diagnostic.

This means AudioFlinger, AudioPolicy and the car-services JNI layer all participate in or depend on the external DSP/mute control path. A replacement must centralize ownership and serialize commands; it must not leave factory and replacement components independently controlling the same hardware.

## 3. Important findings to investigate

### F-01 — Three DSP drivers are loaded unconditionally

The imported `firmware/dumps/mt8163/init.mt8163.rc` loads, in sequence:

1. `carstatus.ko`
2. `cd3313.ko`
3. `ak7604.ko`
4. `fy7604.ko`

Both `factory_init.rc` and `init.mt8163.rc` include the three DSP modules. All three DSP modules contain the `audiomux` misc-device name and registration code markers. They also expose overlapping mute/subwoofer GPIO naming.

The base DTB embedded in `boot.img` has I2C controllers at `0x11007000`, `0x11008000` and `0x11009000` disabled. The single DTBO entry maps symbol `i2c2` to `fragment@11:target`, activates that third controller, and marks all three DSP nodes `status = "okay"` on the same I2C controller at addresses `0x44`, `0x1c` and `0x50`. Each driver has its own `audiomux_misc_device` object pointing to the same misc-device name `audiomux`, and each probe path checks the result of `misc_register` and fails if registration fails. The init script inserts CD3313 first, then AK7604 and FY7604. Therefore, if more than one of these physical chips responds, the drivers compete for the same `/dev/audiomux` class-device name; the first successful probe can block the later probes. AK7604 and FY7604 also request overlapping GPIOs, consistent with those two being alternative hardware variants. This is now a concrete conditional driver-ownership conflict, but the actual fitted chips and probe outcomes still require runtime logs. The reproducible DTBO parser records the enabled nodes and I2C fixup.

### F-02 — Two CD3313 module copies differ

The standalone imported module at `firmware/dumps/mt8163/kernel/cd3313.ko` and the `vendor_lib.zip` member `lib/modules/cd3313.ko` have the same filename but different sizes and SHA-256 values:

- standalone dump: 183,368 bytes; SHA-256 `b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb`; Build ID `dae15c9eb019e9388d15cb88088c202d017ebcf0`
- archive member: 188,520 bytes; SHA-256 `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594`; Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`

These are two distinct driver builds. The standalone reference is a publisher26 build; the vendor archive module is a publisher28 build matching the supplied firmware fingerprint and the `/vendor/lib/modules/cd3313.ko` init path. A detailed function-level comparison is recorded in [CD3313 module variant comparison](CD3313_MODULE_VARIANT_DIFF.md).

### F-03 — Bluetooth SCO microphone profile uses an output channel mask

In `files2/files2/vendor/audio_policy_configuration.xml`, the device port `BT SCO Headset Mic` has role `source` (input) but declares `AUDIO_CHANNEL_OUT_STEREO` at 8/16 kHz. This is inconsistent with an input profile. The corresponding `BT SCO Headset Mic` entry in `files2/files2/system/audio_policy_configuration.xml` correctly uses `AUDIO_CHANNEL_IN_MONO`, confirming that the vendor XML entry is anomalous. Verify which configuration file the runtime actually loads.

### F-04 — Audio policy references an absent include

Both extracted system/vendor `audio_policy_configuration.xml` files include `default_volume_tables.xml`, but that file is absent from the complete extracted tree. Other included policy fragments are present. Determine whether this is an incomplete firmware snapshot or a genuine runtime policy-configuration defect.

### F-05 — Mixer XML contains suspicious text and a duplicate control

`files2/files2/vendor/audio_device.xml` parses as XML, but 22 path elements contain 24 stray literal `>` text characters after self-closing `kctl` elements. The `builtin_Mic_Mic3/turnoff` path also sets `Audio_ADC_3_Switch=Off` twice. This may be ignored by the vendor XML consumer, but it is worth checking rather than silently cleaning the source.

### F-06 — AK7604 default gain differs by init path

- `init.mt8163.rc`: `ro.hw.ak7604.gain` fallback is `-2,-2,2,0`.
- `factory_init.rc`: fallback is `-2,-2,1,0`.

If both init paths are used in different boot/factory modes, the DSP channel gain defaults diverge.

### F-07 — DSP chip selector property not found in the imported property files

The JNI layer and compiled `boot-carservices.vdex` both reference `ro.hw.dsp.chip`, but this property is not present in either imported `build.prop` or `default.prop`, and neither init script sets it. The VDEX occurrence establishes a software read/reference, not a configured value. It may be set by another partition or a vendor property service not present in this dump; verify before designing automatic chip selection.

### F-08 — Vendor CD3313 build fixes two I2C write-cache problems\n\nThe vendor archive module adds exact one-byte I2C transfer-result checks, up to five retries with 20 ms delays, and updates its register cache only after a successful transfer in both `cd3313_write_reg` and raw `cmd_write`. The older standalone reference sends once and updates cache without validating the exact transfer length. This earlier bug finding must not be attributed to the vendor module. See the [binary comparison](CD3313_MODULE_VARIANT_DIFF.md).\n\n### F-09 — CD3313 ioctl payload validation and asynchronous status problems\n\nThe vendor module's ioctl queues commands asynchronously and returns success without waiting for the I2C transaction. The callback has no visible minimum-length checks for commands `0x03`, `0x09`, `0x56` and `0xA3`; command `0x15` reads four words while checking only for more than one. The command buffer is allocated without zero-initialization, so short successful copies can leave stale slab bytes for the callback to consume. The read path formats cached software state and does not perform I2C readback. The raw diagnostic write path also has persistent-file-position and maximum-length NUL-termination risks. Full disassembly evidence and a command table are in [CD3313 ioctl and character-device ABI audit](CD3313_IOCTL_ABI_AUDIT.md).\n\n### F-10 — ExtAudioMuxer JNI over-reads the Java command array by one integer\n\nThe `SendCmdAudioMux(int, int[])` JNI method encodes `_IOC_SIZE` as `array_length + 1`, while passing the pointer returned for exactly `array_length` Java integers. The kernel copies `_IOC_SIZE * 4` bytes, so it reads four bytes past the Java array. This is a confirmed JNI/kernel ABI mismatch. See [ExtAudioMuxer JNI ↔ CD3313 ioctl ABI audit](EXT_AUDIO_MUXER_JNI_ABI_AUDIT.md).\n\n## 4. Duplicate inputs

- `AudioParamOptions.xml` appears in two vendor paths with identical SHA-256.
- `audio.primary.mt8163.so` appears in both `files2` and `vendor_lib` with identical SHA-256.
- `android.hardware.audio@4.0-impl-mediatek.so` also appears in both locations with identical SHA-256.

These are duplicate copies, not separate implementations.

## 5. Next analysis sequence

1. Design a compatibility-safe correction for the JNI one-word over-read and the CD3313 short-payload validation gaps.\n2. Reconcile asynchronous return semantics with the Java callers.\n2. Continue semantic analysis of volume/EQ, event scheduling and physical readback paths.
2. Determine which DSP chip is selected on this board from DT/DTBO, module probe logs and `ro.hw.dsp.chip` provisioning.
3. Map all `/dev/audiomux` clients and their command payloads: AudioFlinger, AudioPolicy, ExtAudioMuxer JNI and each kernel driver.
4. Map primary HAL PCM devices, ALSA mixer controls and XML routes to the actual MT8163 sound-card topology.
5. Record confirmed factory defects separately from configuration anomalies and unknown hardware assumptions.

## Scope and confidence

This is static analysis of the imported firmware snapshot and extracted binaries. The repeatable inventory script is `scripts/firmware/audio_stack_inventory.py`; CI and the analysis workflow both pass. No live device was accessed, no module was loaded, and no hardware registers or GPIOs were changed. Findings F-01, F-04, F-05 and F-07 require additional confirmation; F-02 is a verified hash difference; F-03 is a concrete input/output channel-mask inconsistency in the XML; F-06 is a verified init-file discrepancy.
