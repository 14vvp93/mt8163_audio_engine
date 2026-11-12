# MT8163 read-only runtime collector

This script prepares the remaining Phase 1 runtime evidence without changing the head unit.

## Run from the host

Connect the target head unit with USB debugging enabled and authorize ADB, then run:

\`\`\`bash
bash scripts/device/collect_mt8163_audio_runtime.sh
\`\`\`

For a specific device:

\`\`\`bash
ANDROID_SERIAL=DEVICE_SERIAL bash scripts/device/collect_mt8163_audio_runtime.sh ./runtime-audit
\`\`\`

The output directory is created on the host. Root is used only if \`su -c id\` confirms it is available; otherwise the collector records permission-denied results.

## What it collects

- selected build/board properties (not the device serial number);
- loaded kernel modules;
- ALSA card and PCM enumeration;
- stat/listing information for /dev/audiomux, /dev/carstatus and /dev/fm, without opening those nodes;
- I2C and platform device listings;
- candidate audio-policy file metadata and hashes;
- Android audio services/HAL services;
- AudioFlinger and AudioPolicy dumps;
- process list and process file descriptors pointing at the three vendor device nodes;
- audio-related logcat and kernel log lines.

## Safety boundary

The script does **not**:
- open or read /dev/audiomux, /dev/carstatus or /dev/fm;
- issue ioctl calls or I2C transactions;
- change GPIO, mixer, route, volume or mute state;
- load/unload kernel modules;
- change system properties or files on the target.

All collected output is saved on the host. Review dumpsys/log files before sharing; they may contain package, process or audio-session metadata.

This collector has only been syntax-checked on the host. It has not been run against the physical MT8163 head unit.
