# run-qemu.sh - Smart QEMU runner with fallbacks

set -e

echo "🚀 Running ShadeOS"
echo "=================="

# Common QEMU network options: user-mode networking with RTL8139 NIC
NET_OPTS=(-netdev user,id=net0 -device rtl8139,netdev=net0)
SERIAL_OPTS=(-serial stdio)

# Try ISO first, fallback to direct kernel
if [[ -f shadeOS.iso ]]; then
    echo "Running from ISO..."
    qemu-system-x86_64 -cdrom shadeOS.iso -m 512M "${NET_OPTS[@]}" "${SERIAL_OPTS[@]}"
elif [[ -f kernel.bin ]]; then
    echo "Running kernel directly..."
    qemu-system-x86_64 -kernel kernel.bin -m 512M "${NET_OPTS[@]}" "${SERIAL_OPTS[@]}" -display none
else
    echo "❌ No kernel or ISO found. Run 'make' first."
    exit 1
fi
