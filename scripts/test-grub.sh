#!/bin/zsh
# test-grub.sh - Test GRUB loading specifically

set -e

echo "🧪 Testing GRUB Loading"
echo "======================"

if [[ ! -f shadeOS.iso ]]; then
    echo "❌ shadeOS.iso not found"
    exit 1
fi

echo "📊 ISO Analysis:"
echo "Size: $(stat -c%s shadeOS.iso) bytes"
echo "Type: $(file shadeOS.iso)"

echo ""
echo "🔍 Checking ISO contents:"
if command -v isoinfo &> /dev/null; then
    echo "ISO directory structure:"
    isoinfo -l -i shadeOS.iso
else
    echo "⚠️  isoinfo not available, installing..."
    sudo pacman -S --noconfirm cdrtools
    echo "ISO directory structure:"
    isoinfo -l -i shadeOS.iso
fi

echo ""
echo "🧪 Testing kernel loading with verbose GRUB output:"
echo "Starting QEMU with GRUB debug output..."

qemu-system-x86_64 \
    -cdrom shadeOS.iso \
    -m 512M \
    -serial stdio \
    -d guest_errors \
    -no-reboot \
    -no-shutdown &

QEMU_PID=$!
echo "QEMU started (PID: $QEMU_PID)"
echo "Try selecting a menu option. Press Ctrl+C here to stop."

# Wait for user to test
wait $QEMU_PID || true
