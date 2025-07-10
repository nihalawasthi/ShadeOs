#!/bin/zsh
# debug-boot.sh - Debug the boot process

set -e

echo "🔍 Debugging Boot Process"
echo "========================="

if [[ ! -f kernel.bin ]]; then
    echo "❌ kernel.bin not found"
    exit 1
fi

echo "📊 Checking kernel binary:"
echo "Size: $(stat -c%s kernel.bin) bytes"
echo "Type: $(file kernel.bin)"

echo ""
echo "🔍 Checking multiboot header:"
if hexdump -C kernel.bin | head -10 | grep -q "d6 50 52 e8"; then
    echo "✅ Multiboot2 magic found"
else
    echo "❌ Multiboot2 magic not found"
    echo "First 64 bytes of kernel:"
    hexdump -C kernel.bin | head -4
fi

echo ""
echo "🔍 Checking symbols:"
x86_64-elf-nm kernel.bin | grep -E "(start|kernel_main)"

echo ""
echo "🔍 Testing with QEMU multiboot directly:"
echo "This bypasses GRUB to test if the kernel itself works..."

qemu-system-x86_64 \
    -kernel kernel.bin \
    -m 512M \
    -serial stdio \
    -display none \
    -no-reboot &

QEMU_PID=$!
sleep 3
kill $QEMU_PID 2>/dev/null || true

echo ""
echo "If you saw kernel output above, the kernel works."
echo "If not, there's an issue with the kernel itself."
