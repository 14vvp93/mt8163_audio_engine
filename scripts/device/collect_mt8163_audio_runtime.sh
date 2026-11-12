#!/usr/bin/env bash
set -euo pipefail

# Read-only MT8163 runtime inventory. All output is written on the host.
# This script does not open /dev/audiomux, /dev/carstatus or /dev/fm and
# does not issue ioctls, I2C transactions, GPIO changes or module operations.

ADB_BIN="${ADB:-adb}"
OUT_DIR="${1:-mt8163-audio-runtime-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT_DIR"

ADB=("$ADB_BIN")
if [[ -n "${ANDROID_SERIAL:-}" ]]; then
    ADB+=( -s "$ANDROID_SERIAL" )
fi

if ! "${ADB[@]}" get-state >/dev/null 2>&1; then
    echo "No authorized Android device is available through adb." >&2
    exit 1
fi

ROOT_AVAILABLE=0
if "${ADB[@]}" shell su -c id 2>/dev/null | grep -q 'uid=0'; then
    ROOT_AVAILABLE=1
fi

run_device_read() {
    local name="$1"
    local command="$2"
    local output="$OUT_DIR/$name.txt"
    {
        printf 'COMMAND: %s\n' "$command"
        if [[ "$ROOT_AVAILABLE" -eq 1 ]]; then
            "${ADB[@]}" shell su -c "$command"
        else
            "${ADB[@]}" shell "$command"
        fi
    } >"$output" 2>&1 || true
}

cat > "$OUT_DIR/README.txt" <<EOF2
MT8163 read-only runtime inventory
Root access: $ROOT_AVAILABLE
Device serial: ${ANDROID_SERIAL:-adb default}

This collector writes files only on the host. It does not open or write
/dev/audiomux, /dev/carstatus or /dev/fm; it does not issue ioctls, I2C
transactions, GPIO changes, module loads/unloads, property changes or mixer
writes. Permission-denied output is retained as evidence, not treated as an
empty result.

Review dumpsys and log files before sharing; they may contain process,
package or session metadata.
EOF2

run_device_read fingerprint 'for key in ro.build.fingerprint ro.build.version.release ro.build.version.sdk ro.product.board ro.hardware ro.boot.hardware ro.vendor.build.fingerprint ro.vendor.build.version.sdk; do printf "%s=" "$key"; getprop "$key"; done'
run_device_read kernel_modules 'cat /proc/modules'
run_device_read alsa_cards 'cat /proc/asound/cards'
run_device_read alsa_pcm 'cat /proc/asound/pcm'
run_device_read audio_device_nodes 'ls -l /dev/audiomux /dev/carstatus /dev/fm'
run_device_read i2c_devices 'ls -l /sys/bus/i2c/devices'
run_device_read platform_devices 'ls -l /sys/bus/platform/devices'
run_device_read policy_candidates 'ls -l /vendor/etc/audio_policy_configuration.xml /system/etc/audio_policy_configuration.xml /vendor/etc/audio_policy.conf /system/etc/audio_policy.conf'
run_device_read policy_hashes 'sha256sum /vendor/etc/audio_policy_configuration.xml /system/etc/audio_policy_configuration.xml /vendor/etc/audio_policy.conf /system/etc/audio_policy.conf'
run_device_read audio_services 'service list'
run_device_read hal_services 'lshal'
run_device_read audio_flinger 'dumpsys media.audio_flinger'
run_device_read audio_policy 'dumpsys media.audio_policy'
run_device_read processes 'ps -A'
run_device_read device_logs 'logcat -d -v threadtime | grep -Ei "audiomux|cd3313|ak7604|fy7604|carstatus|fmradio|FM_I2S|HFP|SCO"'
run_device_read kernel_log 'dmesg'

fd_command='for proc in /proc/[0-9]*; do pid=${proc##*/}; for fd in "$proc"/fd/*; do target=$(readlink "$fd" 2>/dev/null) || continue; case "$target" in /dev/audiomux|/dev/carstatus|/dev/fm) printf "%s %s -> %s\n" "$pid" "${fd##*/}" "$target" ;; esac; done; done'
run_device_read audio_device_fd_owners "$fd_command"

# Compact companion log file, restricted to the relevant audio subsystems.
grep -Ei 'audiomux|cd3313|ak7604|fy7604|carstatus|fmradio|FM_I2S|HFP|SCO' \
    "$OUT_DIR/device_logs.txt" "$OUT_DIR/kernel_log.txt" \
    > "$OUT_DIR/audio_related_logs.txt" 2>/dev/null || true

printf 'Read-only inventory saved to: %s\n' "$OUT_DIR"
printf 'Root access used: %s\n' "$ROOT_AVAILABLE"
