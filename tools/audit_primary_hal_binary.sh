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

for tool in readelf sha256sum basename; do
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

echo "===== Identity ====="
echo "File: $binary"
echo "Size: $size bytes"
echo "SHA-256: $sha"
echo "ELF type: $elf_type"
echo "ELF class: $class"
echo "Machine: $machine"
echo "Flags: $flags"

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
echo "===== Dynamic section / SONAME / dependencies ====="
readelf -dW "$binary" || true

echo
echo "===== Dynamic symbols relevant to Android audio HAL ====="
dyn_symbols=$(readelf --dyn-syms -W "$binary")
printf '%s\n' "$dyn_symbols" |
    grep -E '(^|[[:space:]])HMI([[:space:]]|$)|HAL_MODULE_INFO_SYM|audio_hw_device|audio_stream_out|audio_stream_in|adev_open|open_output_stream|open_input_stream|close_output_stream|close_input_stream' ||
    true

if ! printf '%s\n' "$dyn_symbols" | grep -Eq '(^|[[:space:]])HMI([[:space:]]|$)'; then
    echo "ERROR: required Android HAL module export HMI is missing from .dynsym" >&2
    exit 1
fi

echo
echo "===== All exported dynamic symbols (first 250 lines) ====="
printf '%s\n' "$dyn_symbols" | sed -n '1,250p'

echo
echo "===== Notes ====="
echo "This is a static ELF inventory only. It does not prove runtime linker compatibility,"
echo "the exact vendor-extended Android 9 audio HAL ABI, functional routing, or device operation."
