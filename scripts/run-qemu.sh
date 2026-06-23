#!/usr/bin/env bash

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="$root/build/liquidos.img"
serial_log="$root/build/serial.log"
qemu="${LIQUIDOS_QEMU:-$(command -v qemu-system-x86_64 || true)}"
qemu_vga="${LIQUIDOS_QEMU_VGA:-std}"
qemu_display="${LIQUIDOS_QEMU_DISPLAY:-cocoa,zoom-to-fit=off}"
qemu_netdev="${LIQUIDOS_QEMU_NETDEV:-user,id=net0}"
qemu_net_device="${LIQUIDOS_QEMU_NET_DEVICE:-rtl8139,netdev=net0}"
webbridge_host="${LIQUIDOS_WEBBRIDGE_HOST:-127.0.0.1}"
webbridge_port="${LIQUIDOS_WEBBRIDGE_PORT:-8087}"
webbridge_pid=""

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

qemu_webbridge_args=()
if [[ "${LIQUIDOS_WEBBRIDGE:-1}" != "0" ]]; then
    if command -v python3 >/dev/null 2>&1; then
        python3 "$root/scripts/webbridge.py" --host "$webbridge_host" --port "$webbridge_port" > "$root/build/webbridge.log" 2>&1 &
        webbridge_pid="$!"
        echo "LiquidOS WebBridge listening on $webbridge_host:$webbridge_port"
        echo "Liqueia can reach it in the guest at 10.0.2.2:$webbridge_port"
    else
        echo "python3 not found; HTTPS WebBridge disabled" >&2
    fi
fi

cleanup() {
    if [[ -n "$webbridge_pid" ]]; then
        kill "$webbridge_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

"$qemu" \
    -machine pc,accel=tcg \
    -cpu max \
    -m 512 \
    -drive "file=$image,format=raw,if=floppy" \
    -boot a \
    -vga "$qemu_vga" \
    -display "$qemu_display" \
    -serial "file:$serial_log" \
    "${qemu_webbridge_args[@]}" \
    -netdev "$qemu_netdev" \
    -device "$qemu_net_device" \
    -no-reboot
