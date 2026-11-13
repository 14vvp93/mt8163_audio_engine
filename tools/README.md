# MT8163 diagnostic tools

## Collect audio and Device Tree diagnostics

The PowerShell collector reads the head unit over ADB and creates a local ZIP
containing audio routing, ALSA, mixer, kernel-log, configuration and
Device Tree information.

### Requirements

- Windows PowerShell 5.1 or newer.
- Android Platform Tools (`adb`) installed and available in PATH.
- USB debugging enabled on the head unit and this computer authorized.
- Root access is helpful for `dmesg` and the raw Device Tree, but the script
  still collects unprivileged information without root.

### Run

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\collect_mt8163_audio_diag.ps1
```

If more than one ADB device is connected:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\collect_mt8163_audio_diag.ps1 -Serial YOUR_DEVICE_ID
```

Choose a custom output directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\collect_mt8163_audio_diag.ps1 -OutputDir .\MT8163_AudioDiag
```

The script tries `/sys/firmware/fdt` first and then attempts to archive
`/sys/firmware/devicetree/base`. It also pulls common vendor audio
configuration files, including `audio_device.xml`,
`AudioParamOptions.xml`, `audio_policy_configuration.xml`,
`mixer_paths.xml` and `audio_param/` when present.

For FM routing, it additionally captures `/proc/asound/cards`,
`/proc/asound/pcm`, `/proc/fm`, FM device-node metadata, all candidate
`audio_policy_configuration*.xml` files, `fm_cust.cfg` from common firmware
search locations, and read-only pinctrl debugfs snapshots when root access
allows them. It also records `dumpsys media.audio_policy`, `dumpsys media.audio_flinger`,
`getprop`, filtered Bluetooth manager state, and audio media metrics. These
help identify the active output thread/effect chain, negotiated A2DP codec,
offload state/properties, and FM control flags.

### Safety and sharing

The collector is read-only with respect to Android audio settings. It does
not change mixer controls, properties, routing, NVRAM, GPIOs, I²C registers or
system files. Temporary output and the ZIP are created on the computer.

The runtime inventory also captures ALSA PCM substream status, `/dev` node
metadata, processes holding `/dev/audiomux`, `/dev/carstatus`, `/dev/fm` or
`/dev/snd/*`, and I²C device driver symlinks/uevent metadata. The ADB serial is
used to select the target but is intentionally not written into the manifest.

Before sharing the ZIP, review `getprop`, `logcat_audio.txt` and the
kernel logs for device identifiers, application names or other information
you do not want to disclose.

## Audit the factory primary HAL ELF

Run this read-only audit in Linux, WSL or Git Bash against the immutable
firmware extraction:

```sh
sh tools/audit_primary_hal_binary.sh files/vendor/audio.primary.mt8163.so
```

It verifies the factory basename, ELF shared-object type, ELF32 ARM machine,
ARM EABI5 flags and a defined, externally visible `HMI` module symbol. It prints
program/section headers, build ID, SONAME, dependencies, symbol versions,
relocations and dynamic symbols. It warns if size/SHA-256 differ from the
recorded factory baseline. A passing audit does not prove the exact vendor
callback table or runtime linker compatibility. Preserve the reported SHA-256
with the firmware manifest.

## Find the CD3313 JNI native bridge

The Java/Binder route is known, but the native implementation of
`ExtAudioMuxer.nativeInit()` and
`ExtAudioMuxer.SendCmdAudioMux()` is still missing. This scanner searches
a local extracted firmware folder for shared libraries containing the likely
JNI registration/method strings or `/dev/audiomux`. It also includes
`libandroid_servers.so` as a candidate even if those strings are absent.

Run it against the full extracted firmware tree:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_cd3313_jni_candidates.ps1 -Root D:\Desktop\MT8163
```

It creates `CD3313_JNI_Candidates.zip` with a CSV report and copies of
matching libraries. The scanner reads the firmware source folder and does not
modify it. Review the report before sharing the ZIP.


For a broader scan that includes all file types and detects ELF binaries by
their magic bytes (including binaries without a `.so` suffix), use:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_cd3313_native_provider.ps1 -Root D:\Desktop\MT8163
```

This creates `CD3313_Native_Provider.zip`. It records strong CD3313 string
matches separately from generic JNI markers, includes SHA-256 hashes, and
does not execute firmware files. See
`docs/CD3313_NATIVE_PROVIDER_SCAN.md` for interpretation and limitations.
