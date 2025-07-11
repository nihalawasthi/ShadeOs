#!/bin/zsh
# run-qemu.sh - Smart QEMU runner with fallbacks

set -e

echo "🚀 Running ShadeOS"
echo "=================="

# Try ISO first, fallback to direct kernel
if [[ -f shadeOS.iso ]]; then
    echo "Running from ISO..."
    qemu-system-x86_64 -cdrom shadeOS.iso -m 512M
elif [[ -f kernel.bin ]]; then
    echo "Running kernel directly..."
    qemu-system-x86_64 -kernel kernel.bin -m 512M
else
    echo "❌ No kernel or ISO found. Run 'make' first."
    exit 1
fi
