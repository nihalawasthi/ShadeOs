# run.sh - Separate script to run ShadeOS

set -e

echo "🚀 Running ShadeOS"
echo "=================="

if [[ ! -f shadeOS.iso ]]; then
    echo "❌ shadeOS.iso not found. Run ./install.sh first."
    exit 1
fi

echo "Starting QEMU with ShadeOS ISO..."
echo "Instructions:"
echo "1. Wait for GRUB menu to appear"
echo "2. Select 'ShadeOS - 64-bit OS' (first option)"
echo "3. Press Enter"
echo "4. You should see kernel output"
echo ""
echo "Controls:"
echo "- Ctrl+Alt+G to release mouse"
echo "- Ctrl+Alt+F for fullscreen"
echo "- Close window to exit"
echo ""

# Give user a moment to read
sleep 1

qemu-system-x86_64 \
    -cdrom shadeOS.iso \
    -m 512M \
    -serial stdio \
    -enable-kvm 2>/dev/null || qemu-system-x86_64 \
    -cdrom shadeOS.iso \
    -m 512M \
    -serial stdio
