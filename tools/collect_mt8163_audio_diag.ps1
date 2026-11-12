param(
    [string]$Serial = "",
    [string]$OutputDir = "",
    [switch]$SkipDeviceTree
)

$ErrorActionPreference = "Continue"
$adbCommand = Get-Command adb -ErrorAction SilentlyContinue
if (-not $adbCommand) {
    Write-Error "adb was not found in PATH. Install Android Platform Tools first."
    exit 1
}
$adbPath = $adbCommand.Source

if ([string]::IsNullOrWhiteSpace($Serial)) {
    $deviceLines = & $adbPath devices 2>&1
    $online = @($deviceLines | Select-Object -Skip 1 | Where-Object { $_ -match '^\S+\s+device$' })
    if ($online.Count -eq 0) {
        Write-Error "No authorized ADB device found. Enable USB debugging and authorize this computer."
        exit 1
    }
    if ($online.Count -gt 1) {
        Write-Error "More than one ADB device is connected. Re-run with -Serial <device-id>."
        exit 1
    }
    $Serial = ($online[0] -split '\s+')[0]
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = "MT8163_AudioDiag_" + (Get-Date -Format "yyyyMMdd_HHmmss")
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$manifest = @(
    "MT8163 Audio diagnostics",
    "Collected: $(Get-Date -Format o)",
    "ADB serial: $Serial",
    "Read-only collection: yes (temporary local output only)",
    "Device writes: none intended"
)
$manifest | Out-File -FilePath (Join-Path $OutputDir "manifest.txt") -Encoding utf8

function Save-AdbText {
    param(
        [string]$Name,
        [string]$Command,
        [switch]$Root
    )
    $file = Join-Path $OutputDir ($Name + ".txt")
    if ($Root) {
        $result = & $adbPath -s $Serial shell su -c $Command 2>&1
    } else {
        $result = & $adbPath -s $Serial shell $Command 2>&1
    }
    $exitCode = $LASTEXITCODE
    @($result) | Out-File -FilePath $file -Encoding utf8
    Add-Content -Path $file -Value ([Environment]::NewLine + "[adb exit code: $exitCode]")
}

function Pull-RemoteFile {
    param(
        [string]$RemotePath,
        [string]$LocalName
    )
    $dest = Join-Path $OutputDir $LocalName
    $parent = Split-Path -Parent $dest
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    $result = & $adbPath -s $Serial pull $RemotePath $dest 2>&1
    $exitCode = $LASTEXITCODE
    $log = Join-Path $OutputDir "pulled_files.txt"
    Add-Content -Path $log -Value ("{0} -> {1} (exit {2})" -f $RemotePath, $LocalName, $exitCode)
    @($result) | Add-Content -Path $log
}

function Save-ExecOutBinary {
    param(
        [string]$Name,
        [string]$RemoteCommand
    )
    $dest = Join-Path $OutputDir $Name
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $adbPath
    $psi.Arguments = "-s $Serial exec-out $RemoteCommand"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $stream = [System.IO.File]::Create($dest)
    $proc.StandardOutput.BaseStream.CopyTo($stream)
    $stream.Dispose()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0 -or (Get-Item $dest).Length -eq 0) {
        Remove-Item $dest -Force -ErrorAction SilentlyContinue
        Add-Content -Path (Join-Path $OutputDir "device_tree_status.txt") -Value ("FAILED: {0}; exit={1}; {2}" -f $Name, $proc.ExitCode, $stderr)
        return $false
    }
    Add-Content -Path (Join-Path $OutputDir "device_tree_status.txt") -Value ("OK: {0}; {1} bytes" -f $Name, (Get-Item $dest).Length)
    return $true
}

Write-Host "Collecting read-only diagnostics from $Serial ..."

Save-AdbText "getprop" "getprop"
Save-AdbText "system_identity" "uname -a; cat /proc/version; cat /proc/cmdline; cat /proc/uptime"
Save-AdbText "asound" "for f in /proc/asound/cards /proc/asound/pcm /proc/asound/devices /proc/asound/timers; do echo ==== `$f; cat `$f 2>&1; done"
Save-AdbText "sound_devices" "ls -la /dev/snd 2>&1; ls -la /dev/audiomux 2>&1; ls -la /dev/i2c-* 2>&1"
Save-AdbText "modules" "cat /proc/modules; echo ==== lsmod; lsmod 2>&1"
Save-AdbText "tinymix" "command -v tinymix 2>&1; tinymix 2>&1"
Save-AdbText "audio_flinger" "dumpsys media.audio_flinger"
Save-AdbText "audio_policy" "dumpsys media.audio_policy"
Save-AdbText "audio_service" "dumpsys audio"
Save-AdbText "i2c_devices" "ls -la /sys/bus/i2c/devices 2>&1; cat /proc/bus/i2c/devices 2>&1"
Save-AdbText "block_partitions" "ls -la /dev/block/by-name 2>&1; cat /proc/partitions"
Save-AdbText "device_tree_listing" "ls -la /sys/firmware/devicetree/base 2>&1; ls -la /proc/device-tree 2>&1; cat /sys/firmware/devicetree/base/model 2>&1; tr '\000' '\n' </sys/firmware/devicetree/base/compatible 2>/dev/null"
Save-AdbText "audio_config_file_list" "for d in /vendor/etc /system/etc; do echo ==== `$d; find `$d -maxdepth 4 -type f 2>/dev/null | grep -iE 'audio|mixer|codec|i2s'; done"

# FM-specific, read-only state needed to correlate the MT6627 driver, HAL and ALSA route.
Save-AdbText "fm_proc_status" "echo ==== /proc/fm; ls -l /proc/fm 2>&1; cat /proc/fm 2>&1; echo ==== /dev/fm; ls -l /dev/fm 2>&1" -Root
Save-AdbText "fm_customer_config_candidates" "for d in /vendor/firmware /system/etc/firmware /vendor/etc/firmware /odm/etc/firmware /lib/firmware /system/vendor/firmware; do echo ==== `$d; find `$d -maxdepth 4 -type f -iname fm_cust.cfg -print 2>/dev/null | while read f; do echo ==== `$f; cat `$f 2>&1; done; done" -Root
Save-AdbText "audio_policy_candidates" "for d in /odm/etc /vendor/etc /system/etc; do echo ==== `$d; find `$d -maxdepth 2 -type f -iname 'audio_policy_configuration*.xml' -print 2>/dev/null; done"
Save-AdbText "pinctrl_audio" "find /sys/kernel/debug/pinctrl -maxdepth 2 -type f 2>/dev/null | grep -E '/(pinmux-pins|pins|pinmux-functions|pinconf-pins)$' | while read f; do echo ==== `$f; cat `$f 2>&1; done" -Root
Save-AdbText "dmesg_root" "dmesg" -Root
Save-AdbText "dmesg_user" "dmesg"
Save-AdbText "logcat_audio" "logcat -d -t 5000 2>&1 | grep -iE 'audio|cd3313|i2s|codec|audiomux|mtk' | tail -n 3000"

Pull-RemoteFile "/vendor/etc/audio_device.xml" "vendor/audio_device.xml"
Pull-RemoteFile "/vendor/etc/AudioParamOptions.xml" "vendor/AudioParamOptions.xml"
Pull-RemoteFile "/vendor/etc/audio_policy_configuration.xml" "vendor/audio_policy_configuration.xml"
Pull-RemoteFile "/odm/etc/audio_policy_configuration.xml" "odm/audio_policy_configuration.xml"
Pull-RemoteFile "/odm/etc/audio_policy_configuration_a2dp_offload_disabled.xml" "odm/audio_policy_configuration_a2dp_offload_disabled.xml"
Pull-RemoteFile "/vendor/etc/audio_policy_configuration_a2dp_offload_disabled.xml" "vendor/audio_policy_configuration_a2dp_offload_disabled.xml"
Pull-RemoteFile "/system/etc/audio_policy_configuration_a2dp_offload_disabled.xml" "system/audio_policy_configuration_a2dp_offload_disabled.xml"
Pull-RemoteFile "/vendor/firmware/fm_cust.cfg" "vendor/firmware/fm_cust.cfg"
Pull-RemoteFile "/system/etc/firmware/fm_cust.cfg" "system/etc/firmware/fm_cust.cfg"
Pull-RemoteFile "/vendor/etc/firmware/fm_cust.cfg" "vendor/etc/firmware/fm_cust.cfg"
Pull-RemoteFile "/odm/etc/firmware/fm_cust.cfg" "odm/etc/firmware/fm_cust.cfg"
Pull-RemoteFile "/lib/firmware/fm_cust.cfg" "lib/firmware/fm_cust.cfg"
Pull-RemoteFile "/system/vendor/firmware/fm_cust.cfg" "system/vendor/firmware/fm_cust.cfg"
Pull-RemoteFile "/vendor/etc/audio_policy.conf" "vendor/audio_policy.conf"
Pull-RemoteFile "/vendor/etc/audio_effects.xml" "vendor/audio_effects.xml"
Pull-RemoteFile "/vendor/etc/mixer_paths.xml" "vendor/mixer_paths.xml"
Pull-RemoteFile "/vendor/etc/audio_param" "vendor/audio_param"
Pull-RemoteFile "/system/etc/audio_policy.conf" "system/audio_policy.conf"
Pull-RemoteFile "/system/etc/audio_effects.xml" "system/audio_effects.xml"

if (-not $SkipDeviceTree) {
    $null = Save-ExecOutBinary "device-tree.dtb" "su -c 'cat /sys/firmware/fdt'"
    if (-not (Test-Path (Join-Path $OutputDir "device-tree.dtb"))) {
        $null = Save-ExecOutBinary "device-tree.dtb" "cat /sys/firmware/fdt"
    }
    if (-not (Test-Path (Join-Path $OutputDir "device-tree.dtb"))) {
        $null = Save-ExecOutBinary "device-tree-tree.tar" "su -c 'tar -C /sys/firmware/devicetree/base -cf - .'"
    }
    if (-not (Test-Path (Join-Path $OutputDir "device-tree-tree.tar"))) {
        $null = Save-ExecOutBinary "device-tree-tree.tar" "sh -c 'tar -C /sys/firmware/devicetree/base -cf - .'"
    }
}

$zipPath = "$OutputDir.zip"
try {
    Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $zipPath -Force
    Write-Host "Diagnostics archive created: $zipPath"
} catch {
    Write-Warning "Could not create ZIP automatically. Diagnostic files are in: $OutputDir"
}
Write-Host "Review the archive before sharing: getprop, logcat and dmesg can contain device-specific identifiers or app names."
