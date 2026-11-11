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

### Safety and sharing

The collector is read-only with respect to Android audio settings. It does
not change mixer controls, properties, routing, NVRAM, GPIOs, I²C registers or
system files. Temporary output and the ZIP are created on the computer.

Before sharing the ZIP, review `getprop`, `logcat_audio.txt` and the
kernel logs for device identifiers, application names or other information
you do not want to disclose.
