#!/system/bin/sh
# Read-only diagnostic collector for MT8163 Android head units.
# It does not open PCM streams, load/unload modules, write sysfs, or issue ioctls.

section() {
    echo
    echo "===== $1 ====="
}

show_file() {
    if [ -r "$1" ]; then
        echo "--- $1"
        cat "$1" 2>&1
    else
        echo "--- $1 (not readable or absent)"
    fi
}

section "Build and board properties"
getprop ro.build.version.release 2>&1
getprop ro.build.version.sdk 2>&1
getprop ro.product.board 2>&1
getprop ro.hardware 2>&1
getprop ro.hw.dsp.chip 2>&1

section "ALSA cards and PCM devices"
show_file /proc/asound/cards
show_file /proc/asound/pcm
show_file /proc/asound/version

section "Sound class"
if [ -d /sys/class/sound ]; then
    ls -la /sys/class/sound 2>&1
else
    echo "/sys/class/sound absent"
fi

section "Relevant device nodes"
ls -l /dev/audiomux /dev/carstatus /dev/snd 2>&1

section "Loaded audio and DSP modules"
if [ -r /proc/modules ]; then
    grep -iE 'audio|snd|pcm|cd3313|ak7604|fy7604|carstatus|mtk' /proc/modules 2>&1
else
    echo "/proc/modules not readable"
fi

section "Relevant kernel messages (permission may be restricted)"
dmesg 2>&1 | grep -iE 'audio|alsa|snd|i2c|cd3313|ak7604|fy7604|audiomux|carstatus' | tail -n 250

section "Processes holding audiomux/carstatus (best effort)"
for fd in /proc/[0-9]*/fd/*; do
    [ -L "$fd" ] || continue
    target=$(readlink "$fd" 2>/dev/null) || continue
    case "$target" in
        /dev/audiomux|/dev/carstatus)
            echo "$fd -> $target"
            ;;
    esac
done

section "End"
echo "Collection is read-only. Permission-denied output is expected on some builds."
