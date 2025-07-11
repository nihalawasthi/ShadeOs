#!/bin/zsh
# test.sh - Unified kernel and GRUB testing

set -e

MODE=${1:-kernel}

usage() {
    echo "Usage: $0 [kernel|grub]"
    echo "  kernel: Test direct kernel boot (default)"
    echo "  grub:   Test GRUB/ISO boot"
    exit 1
}

case "$MODE" in
    kernel)
        echo "🧪 Testing Kernel (direct boot)"
        echo "=============================="
if [[ ! -f kernel.bin ]]; then
    echo "❌ No kernel binary found"
    exit 1
fi
        echo "Running QEMU with kernel.bin..."
        qemu-system-x86_64 \
            -kernel kernel.bin \
        -m 512M \
            -serial stdio \
        -display none \
        -no-reboot \
            -no-shutdown &
        QEMU_PID=$!
        echo "QEMU started (PID: $QEMU_PID)"
        echo "Wait a few seconds to observe output. Press Ctrl+C to stop."
        wait $QEMU_PID || true
        ;;
    grub)
        echo "🧪 Testing GRUB/ISO Boot"
        echo "========================"
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
        wait $QEMU_PID || true
        ;;
    *)
        usage
        ;;
esac
