#!/bin/zsh
# debug-multiboot.sh - Check multiboot header

set -e

echo "🔍 Debugging Multiboot Header"
echo "============================="

if [[ ! -f kernel.bin ]]; then
    echo "❌ kernel.bin not found"
    exit 1
fi

echo "📊 Kernel Analysis:"
echo "Size: $(stat -c%s kernel.bin) bytes"
echo "Type: $(file kernel.bin)"

echo ""
echo "🔍 Checking multiboot magic at start of file:"
hexdump -C kernel.bin | head -5

echo ""
echo "🔍 Looking for multiboot2 magic (0xE85250D6):"
if hexdump -C kernel.bin | grep -q "d6 50 52 e8"; then
    echo "✅ Found multiboot2 magic!"
else
    echo "❌ Multiboot2 magic not found!"
fi

echo ""
echo "🔍 Checking sections:"
x86_64-elf-objdump -h kernel.bin

echo ""
echo "🔍 Checking symbols:"
x86_64-elf-nm kernel.bin | grep -E "(start|kernel_main)"

echo ""
echo "🔍 Entry point:"
x86_64-elf-readelf -h kernel.bin | grep "Entry point"
