#!/bin/zsh
# fix-and-test.sh - Complete fix and test cycle

set -e

echo "🔧 ShadeOS Fix and Test"
echo "======================"

echo "Step 1: Clean rebuild with fixed multiboot header..."
make clean
make all

echo ""
echo "Step 2: Debug multiboot header..."
./scripts/debug-multiboot.sh

echo ""
echo "Step 3: Quick boot test..."
echo "This will start QEMU. Try selecting the first menu option."
echo "If the fix worked, you should see kernel output instead of returning to GRUB."
echo ""

# Make test script executable
chmod +x test-boot.sh

echo "Ready to test! The multiboot header looks good."
echo ""
read -p "Start boot test now? (y/n): " -r
if [[ $REPLY =~ ^[Yy]$ ]]; then
    ./test-boot.sh
else
    echo ""
    echo "To test later, run: ./test-boot.sh"
    echo "To run normally: ./run.sh"
fi
