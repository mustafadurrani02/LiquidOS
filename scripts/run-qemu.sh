#!/usr/bin/env bash

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="$root/build/liquidos.img"
serial_log="$root/build/serial.log"
qemu="${LIQUIDOS_QEMU:-$(command -v qemu-system-x86_64 || true)}"
qemu_vga="${LIQUIDOS_QEMU_VGA:-std}"
qemu_display="${LIQUIDOS_QEMU_DISPLAY:-cocoa,zoom-to-fit=off}"

if [[ "${1:-}" != "--no-build" ]]; then
    "$root/scripts/build-macos.sh"
fi

if [[ -z "$qemu" || ! -x "$qemu" ]]; then
    echo "QEMU was not found. Install it with: brew install qemu" >&2
    exit 1
fi

if [[ ! -f "$image" ]]; then
    echo "Missing $image. Run scripts/build-macos.sh first." >&2
    exit 1
fi

rm -f "$serial_log"

exec "$qemu" \
    -machine pc,accel=tcg \
    -cpu max \
    -m 512 \
    -drive "file=$image,format=raw,if=floppy" \
    -boot a \
    -vga "$qemu_vga" \
    -display "$qemu_display" \
    -serial "file:$serial_log" \
    -nic none \
    -no-reboot
