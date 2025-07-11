#!/bin/zsh
# fix.sh - Unified fix and test script

set -e

MODE=${1:-all}

usage() {
    echo "Usage: $0 [kernel|grub|all]"
    echo "  kernel: Clean build, debug multiboot, test kernel"
    echo "  grub:   Clean build, verify ISO, debug boot, test GRUB"
    echo "  all:    Do both (default)"
    exit 1
}

fix_kernel() {
    echo "🔧 Step 1: Clean rebuild with fixed multiboot header..."
    make clean
    make all
    echo ""
    echo "🔧 Step 2: Debug multiboot header..."
    ./debug.sh multiboot
    echo ""
    echo "🔧 Step 3: Quick kernel boot test..."
    ./test.sh kernel
}

fix_grub() {
    echo "🔧 Step 1: Clean rebuild..."
    make clean
    make all
    echo ""
    echo "🔧 Step 2: Verify ISO structure..."
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
    echo "🔧 Step 3: Debug boot process..."
    ./debug.sh boot
    echo ""
    echo "🔧 Step 4: Test with fixed GRUB..."
    ./test.sh grub
}

case "$MODE" in
    kernel)
        fix_kernel
        ;;
    grub)
        fix_grub
        ;;
    all)
        fix_kernel
        echo ""
        fix_grub
        ;;
    *)
        usage
        ;;
esac 