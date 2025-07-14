# debug.sh - Unified debug script: auto, boot, multiboot

set -e

MODE=${1:-auto}

usage() {
    echo "Usage: $0 [auto|boot|multiboot]"
    echo "  auto:      Auto-debug and auto-fix (default)"
    echo "  boot:      Debug boot process (kernel.bin)"
    echo "  multiboot: Debug/check multiboot header (kernel.bin)"
    exit 1
}

case "$MODE" in
    auto)
        echo "🐛 Auto-Debug Mode"
        echo "=================="
        auto_fix() {
            echo "🔧 Attempting auto-fix..."
            if ! command -v xorriso &> /dev/null; then
                echo "Installing xorriso..."
                sudo pacman -S --noconfirm xorriso
                return 0
            fi
            if ! command -v x86_64-elf-gcc &> /dev/null; then
                echo "Installing cross-compiler..."
                if ! command -v yay &> /dev/null; then
                    cd /tmp && git clone https://aur.archlinux.org/yay.git && cd yay && makepkg -si --noconfirm
                fi
                yay -S --noconfirm x86_64-elf-gcc x86_64-elf-binutils
                return 0
            fi
            if ! command -v cargo &> /dev/null; then
                echo "Installing Rust toolchain..."
                curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
                source "$HOME/.cargo/env"
                rustup default nightly
                rustup target add x86_64-unknown-none
                return 0
            fi
            if [[ -f kernel.bin ]]; then
                echo "Checking multiboot header..."
                if ! x86_64-elf-objdump -h kernel.bin | grep -q ".multiboot"; then
                    echo "❌ Multiboot section missing - this could cause boot failure"
                    echo "Rebuilding with fixed bootloader..."
                    make clean
                    make all
                    return 0
                fi
            fi
            echo "Attempting clean build..."
            make clean
            if make all; then
                echo "✅ Clean build successful"
                if [[ -f kernel.bin ]]; then
                    echo "Kernel size: $(stat -c%s kernel.bin) bytes"
                    echo "Kernel type: $(file kernel.bin)"
                    if x86_64-elf-nm kernel.bin | grep -q "kernel_main"; then
                        echo "✅ kernel_main symbol found"
                    else
                        echo "❌ kernel_main symbol missing"
                    fi
                fi
                return 0
            fi
            echo "❌ Auto-fix failed. Showing detailed build output:"
            make clean
            make all
            return 1
        }
        if auto_fix; then
            echo "✅ Issue resolved automatically"
            exit 0
        else
            echo "❌ Manual intervention required"
            echo ""
            echo "🔍 Debug information:"
            echo "Current directory: $(pwd)"
            echo "Files present:"
            ls -la
            echo ""
            echo "If kernel.bin exists, try: ./run.sh"
            exit 1
        fi
        ;;
    boot)
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
        ;;
    multiboot)
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
        ;;
    *)
        usage
        ;;
esac
