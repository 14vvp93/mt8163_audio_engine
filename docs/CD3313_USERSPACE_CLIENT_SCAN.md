# CD3313 userspace-client scan

## Result

The GitHub Actions workflow **Scan vendor archive for CD3313 userspace clients** completed successfully on 2026-11-10.

- Workflow run: [42804202355](https://github.com/14march93/mt8163_audio_engine/actions/runs/42804202355)
- Artifact: `audiomux-userspace-client-scan`
- Artifact SHA-256: `21267401f156cccad16565ae0dd8ef89bbbb7a6d6331df212450d95277e0a6ee`
- Scope: the audio-related files extracted from the repository's `files.zip` archive (36 files identified by the scan).

## Findings

No direct `/dev/audiomux` ioctl client was identified in this 36-file archive. A separate HEQ APK analysis has since identified a high-level CD3313-aware ASP client that uses android.audio.AudioEffect; its hidden transport remains unresolved.

The binary and text scan found no actual string occurrences of:

- `/dev/audiomux` / `audiomux`
- `cd3313`
- `dsp_i2c_driver`
- `STATUS_WRITE_CMD`
- `STATUS_READ_CMD`

The text/source match report was empty. The binary string report contained generic `ioctl` references in `audio.primary.mt8163.so`, but the associated strings identify unrelated interfaces, including HDMI audio settings, FM controls, Bluetooth SCO, and CCCI/modem devices. These matches are not evidence of a CD3313 caller.

The archive also contains `audiocmdservice_atci`. Its previously analyzed strings and imports point to the MediaTek ACF/HCF/BES audio-parameter path; this scan did not find a `/dev/audiomux` or CD3313 reference in it. Do not treat that service as the CD3313 ioctl client.

## Extracted audio HAL archive follow-up

The second safe extraction of files.zip is recorded in [workflow run 42885005088](https://github.com/14march93/mt8163_audio_engine/actions/runs/42885005088). It produced 36 files: audio policy/configuration XML, audio.primary.mt8163.so, MediaTek HIDL audio HAL libraries/services, audiocmdservice_atci, and related audio libraries. It contained no kernel .ko modules and no Android framework JAR.

A targeted strings scan of the supplied binaries found:
- audio.primary.mt8163.so: generic MediaTek audio/HAL ioctl strings and normal device paths, but no audiomux or cd3313 strings.
- android.hardware.audio@4.0-impl-mediatek.so and vendor.mediatek.hardware.audio@4.1.so: no audiomux or cd3313 strings.
- audiocmdservice_atci: /dev/vndbinder, but no audiomux or cd3313 strings.

These are negative string-scan results on stripped binaries, not proof that no indirect ioctl or binder path exists. They do show that the supplied audio HAL archive does not expose an obvious direct CD3313 client. The matching Java/Binder implementation has since been recovered from boot-carservices.vdex in the files2 archive; the remaining missing layer is its native JNI transport.

## HEQ APK follow-up: high-level CD3313 caller found

A separate full JADX analysis of HEQ.apk found explicit CD3313 support:

- EqUtils.ASP_CHIP_3313 is "cd3313".
- The app selects the chip through ro.hw.dsp.chip and treats cd3313 as an ASP backend.
- AspSettings calls the vendor android.audio.AudioEffect API for four-channel balance, bass, treble, loudness, subwoofer and master volume.
- AspFragment reduces its 12 UI bands to two averages (low six -> bass, high six -> treble).
- The four-channel balance API uses FL/FR/RL/RR values in the same 0..15 domain as the driver's 0x15 speaker command.

This resolves the earlier ambiguity: HEQ is a high-level CD3313-aware ASP client. It does not contradict the negative scan of files.zip, because the HEQ APK is a separate artifact and the Java code calls a hidden vendor framework API.

The HEQ APK contains no implementation of android.audio.AudioEffect and no direct /dev/audiomux open/ioctl call. The hidden API and Binder service were subsequently recovered from boot-carservices.vdex. AudioEffect calls ISourceService.doExtAudioEffect(); SourceService forwards ordinary command IDs and integer arrays to native ExtAudioMuxer.SendCmdAudioMux(), while command 1000 writes sys.audio.lr.balance. ExtAudioMuxer checks /dev/audiomux and SourceService construction calls nativeInit(). The native JNI implementation and exact ioctl request are still missing. The Java payload shapes are now known; the driver consumes only selected words for some commands.

## Related MT8163 board lead: carstatus device and ACC source

A separate public MT8163 board-support repository, `Tec4Sho/android_device_alps_tb8163p3_bsp`, provides a useful lead for the origin of the driver's ACC state. This is related-platform evidence, not proof that the exact target head unit uses the same board image.

The board configuration contains the following static evidence:

- `recovery/root/init.mt8163.rc` loads both `/vendor/lib/modules/carstatus.ko` and `/vendor/lib/modules/cd3313.ko` during initialization.
- `recovery/root/factory_init.rc` also loads both modules.
- `recovery/root/sbin/init.insmod.cfg` lists `carstatus.ko` immediately before `cd3313.ko`.
- `recovery/root/ueventd.rc` assigns `/dev/carstatus` mode `0664`, owner `root`, group `system`.

Sources:
- [MT8163 init module loading](https://github.com/Tec4Sho/android_device_alps_tb8163p3_bsp/blob/dae4caeb433746ee84e42443ea1b904f3b590a59/recovery/root/init.mt8163.rc)
- [Module load list](https://github.com/Tec4Sho/android_device_alps_tb8163p3_bsp/blob/dae4caeb433746ee84e42443ea1b904f3b590a59/recovery/root/sbin/init.insmod.cfg)
- [carstatus device-node permissions](https://github.com/Tec4Sho/android_device_alps_tb8163p3_bsp/blob/dae4caeb433746ee84e42443ea1b904f3b590a59/recovery/root/ueventd.rc)

This establishes that at least one MT8163 Android 9 BSP carries a dedicated `carstatus` kernel module and exposes `/dev/carstatus` to the system group, while loading it alongside the CD3313 module. It makes `carstatus.ko` a strong candidate to investigate for vehicle-state/ACC handling.

It does **not** yet establish:
- that `carstatus.ko` itself reads the physical ACC wire rather than receiving MCU/CAN data;
- the ioctl/read/write ABI of `/dev/carstatus`;
- which process opens `/dev/carstatus`;
- whether that process sends CD3313 command `0x11`, reason `2`;
- that this board's wiring or module revision matches the user's head unit.

The current supplied audio archive does not contain `carstatus.ko` or a framework JAR, and the previous 36-file scan was scoped to audio-related files only. The next high-value artifacts are (1) the native library that registers ExtAudioMuxer.nativeInit() and SendCmdAudioMux(), and (2) the matching carstatus.ko plus its userspace client/service. The first closes the CD3313 ioctl path; the second may close the ACC-to-CD3313 reason-2 path. Until then, keep the ioctl framing and ACC sender classified as unresolved.

## files2 boot-carservices.vdex follow-up: Java/Binder client recovered

The larger files2 archive includes framework/boot-carservices.vdex and its matching OAT. The VDEX contains android.audio.AudioEffect, ISourceService (Stub/Proxy), SourceService and ExtAudioMuxer. See [CD3313_HIDDEN_AUDIOEFFECT_API.md](CD3313_HIDDEN_AUDIOEFFECT_API.md) for the recovered command arrays and path mapping.

The most important cross-check is setBalance(FL,FR,RL,RR): Java sends command 21 (0x15) with four integers, exactly matching the kernel driver's four speaker-level arguments. Bass, loudness, treble, input gain and subwoofer command IDs also match implemented driver IDs. This confirms the high-level CD3313 control API; it does not yet reveal the native ioctl framing.

## What this means for the ABI

The kernel module's dispatcher-side behavior remains recoverable from `cd3313.ko`, but the caller-side ioctl encoding and argument structures are not established by this archive. In particular, do not invent an ioctl magic/type, request word, or command payload layout and do not send experimental writes to the live device.

The driver-side reconstruction currently indicates that the request word carries direction, command ID, and a size field, and that the event callback reads command arguments after the 32-bit request word. This is not enough by itself to claim a fully validated userspace ABI.

## Next evidence to obtain

1. The native JNI library registering android.sourceservice.ExtAudioMuxer.nativeInit() and SendCmdAudioMux(); scan system/vendor shared libraries for SendCmdAudioMux, ExtAudioMuxer and /dev/audiomux.
2. The exact ioctl request-word construction and userspace buffer framing from that native implementation.
3. carstatus.ko and its userspace client/service to trace ACC state into CD3313 mute reason 2.
4. If a rooted target becomes available, read-only runtime evidence such as device-node permissions, process file descriptors, and relevant app/service package inventory. Do not issue write ioctls during discovery.

## Limitations

A negative string scan is not proof that no client exists on the actual head unit. The client may be outside files.zip, may construct the device path dynamically, or may use an indirect library call. This result only narrows the original 36-file audio archive and prevents us from incorrectly attributing unrelated HAL ioctl calls to CD3313. The subsequent files2 VDEX analysis establishes the high-level Java/Binder client and its command mapping; only the native JNI transport and exact ioctl request remain unresolved.


## 2026-11-11 expanded firmware artifact follow-up: native client confirmed

The negative findings above apply to the original 36-file audio-only archive
and the bounded `files.zip` scan. The larger firmware-library-analysis
artifact includes `lib/lib/libcarservices_jni.so`, which is the native
provider for the recovered Java/Binder API.

Its JNI registration identifies `android/sourceservice/ExtAudioMuxer`.
Static ARM disassembly confirms that `SendCmdAudioMux(int,int[])` opens
`/dev/audiomux` with `O_WRONLY` and calls `ioctl()`; `nativeInit()`
opens it read-only and queries the `cd3313` identifier. This closes the
previously missing userspace transport link. It does not identify a separate
ordinary app process directly opening the node: the native call executes in
the sourceinfo service process.

The exact request formula and its extra-word discrepancy are recorded in
[CD3313_IOCTL_ABI.md](CD3313_IOCTL_ABI.md). The extra copied word has no
established value, so this is not yet authorization to implement or execute
a direct device client. No live ioctl or hardware command has been sent.
