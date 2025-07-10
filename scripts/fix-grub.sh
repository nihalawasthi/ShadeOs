#!/bin/zsh
# fix-grub.sh - Fix GRUB configuration and rebuild

set -e

echo "🔧 Fixing GRUB Configuration"
echo "============================"

echo "Step 1: Clean rebuild..."
make clean
make all

echo ""
echo "Step 2: Verify ISO structure..."
if command -v isoinfo &> /dev/null; then
    echo "ISO contents:"
    isoinfo -l -i shadeOS.iso | grep -E "(boot|kernel|grub)"
else
    echo "Installing isoinfo..."
    sudo pacman -S --noconfirm cdrtools
    echo "ISO contents:"
    isoinfo -l -i shadeOS.iso | grep -E "(boot|kernel|grub)"
fi

echo ""
echo "Step 3: Test direct kernel boot..."
./debug-boot.sh

echo ""
echo "Step 4: Test with fixed GRUB..."
echo "Starting QEMU with verbose GRUB output..."
echo "Select the first menu option and watch for error messages."

qemu-system-x86_64 \
    -cdrom shadeOS.iso \
    -m 512M \
    -serial stdio
