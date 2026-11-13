#!/usr/bin/env sh
# Read-only ELF/ABI inventory for the recovered MT8163 primary audio HAL.
# Usage: sh tools/audit_primary_hal_binary.sh path/to/audio.primary.mt8163.so

set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <audio.primary.mt8163.so>" >&2
    exit 2
fi

binary=$1
if [ ! -f "$binary" ] || [ ! -r "$binary" ]; then
    echo "ERROR: file is missing or unreadable: $binary" >&2
    exit 2
fi

for tool in readelf sha256sum basename awk wc tr grep sed; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required tool not found: $tool" >&2
        exit 2
    fi
done

filename=$(basename "$binary")
if [ "$filename" != "audio.primary.mt8163.so" ]; then
    echo "ERROR: expected factory filename audio.primary.mt8163.so, got: $filename" >&2
    exit 1
fi

header=$(readelf -hW "$binary")
elf_type=$(printf '%s\n' "$header" | awk -F: '/Type:/ {gsub(/^[ \t]+/, "", $2); print $2}')
class=$(printf '%s\n' "$header" | awk -F: '/Class:/ {gsub(/^[ \t]+/, "", $2); print $2}')
machine=$(printf '%s\n' "$header" | awk -F: '/Machine:/ {gsub(/^[ \t]+/, "", $2); print $2}')
flags=$(printf '%s\n' "$header" | awk -F: '/Flags:/ {sub(/^[ \t]+/, "", $2); print $2}')
size=$(wc -c < "$binary" | tr -d ' ')
sha=$(sha256sum "$binary" | awk '{print $1}')
expected_sha=112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4
expected_size=1671852

echo "===== Identity ====="
echo "File: $binary"
echo "Size: $size bytes"
echo "SHA-256: $sha"
echo "ELF type: $elf_type"
echo "ELF class: $class"
echo "Machine: $machine"
echo "Flags: $flags"
if [ "$sha" != "$expected_sha" ] || [ "$size" != "$expected_size" ]; then
    echo "WARNING: artifact differs from the recorded factory baseline."
    echo "Expected size/SHA-256: $expected_size / $expected_sha"
fi

if [ "$elf_type" != "DYN (Shared object file)" ]; then
    echo "ERROR: expected an ELF shared object (ET_DYN)" >&2
    exit 1
fi

if [ "$class" != "ELF32" ]; then
    echo "ERROR: expected ELF32 for the recovered ARM32 target" >&2
    exit 1
fi

case "$machine" in
    *ARM*) ;;
    *)
        echo "ERROR: expected ARM machine type" >&2
        exit 1
        ;;
esac

case "$flags" in
    *"Version5 EABI"*) ;;
    *)
        echo "ERROR: expected ARM EABI5 flags" >&2
        exit 1
        ;;
esac

echo
echo "===== Program headers ====="
readelf -lW "$binary"

echo
echo "===== Section headers ====="
readelf -SW "$binary"

echo
echo "===== ELF notes / build ID ====="
readelf -nW "$binary"

echo
echo "===== Dynamic section / SONAME / dependencies ====="
readelf -dW "$binary" || true

echo
echo "===== Dynamic symbol version requirements ====="
readelf --version-info -W "$binary" || true

echo
echo "===== Relocations ====="
readelf -rW "$binary"

echo
echo "===== Dynamic symbols relevant to Android audio HAL ====="
dyn_symbols=$(readelf --dyn-syms -W "$binary")
printf '%s\n' "$dyn_symbols" |
    grep -E '(^|[[:space:]])HMI([[:space:]]|$)|HAL_MODULE_INFO_SYM|audio_hw_device|audio_stream_out|audio_stream_in|adev_open|open_output_stream|open_input_stream|close_output_stream|close_input_stream' ||
    true

hmi_export=$(printf '%s\n' "$dyn_symbols" |
    awk '$8 == "HMI" && ($5 == "GLOBAL" || $5 == "WEAK") && $6 == "DEFAULT" && $7 != "UND" { print; found = 1 } END { if (!found) exit 1 }') || {
    echo "ERROR: HMI is not a defined GLOBAL/WEAK DEFAULT dynamic export" >&2
    exit 1
}

echo
echo "===== Confirmed HAL module export ====="
printf '%s\n' "$hmi_export"

echo
echo "===== All exported dynamic symbols (first 250 lines) ====="
printf '%s\n' "$dyn_symbols" | sed -n '1,250p'

echo
echo "===== Notes ====="
echo "This is a static ELF inventory only. It does not prove runtime linker compatibility,"
echo "the exact vendor-extended Android 9 audio HAL ABI, functional routing, or device operation."
