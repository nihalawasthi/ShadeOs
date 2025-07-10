#!/bin/zsh
# test-boot.sh - Quick boot test

set -e

echo "🚀 Testing ShadeOS Boot"
echo "======================"

if [[ ! -f shadeOS.iso ]]; then
    echo "❌ shadeOS.iso not found. Run make all first."
    exit 1
fi

echo "Starting QEMU with ShadeOS..."
echo "Instructions:"
echo "1. Select 'ShadeOS - Lightweight 64-bit OS' from the menu"
echo "2. Press Enter"
echo "3. You should see kernel output instead of returning to GRUB"
echo "4. Press Ctrl+Alt+G to release mouse if needed"
echo "5. Close QEMU window or press Ctrl+C here to exit"
echo ""
echo "If it works, you'll see:"
echo "- Loading ShadeOS..."
echo "- Entering 64-bit mode..."
echo "- ShadeOS kernel output"
echo ""

echo -n "Press Enter to start QEMU..."
read

# Run with both serial and VGA output
qemu-system-x86_64 \
    -cdrom shadeOS.iso \
    -m 512M \
    -serial stdio \
    -vga std
