#!/bin/zsh
# debug.sh - Enhanced auto-debug with bootloader fixes

set -e

echo "🐛 Auto-Debug Mode"
echo "=================="

# Check common issues and auto-fix
auto_fix() {
    echo "🔧 Attempting auto-fix..."
    
    # Fix 1: Missing xorriso
    if ! command -v xorriso &> /dev/null; then
        echo "Installing xorriso..."
        sudo pacman -S --noconfirm xorriso
        return 0
    fi
    
    # Fix 2: Missing cross-compiler
    if ! command -v x86_64-elf-gcc &> /dev/null; then
        echo "Installing cross-compiler..."
        if ! command -v yay &> /dev/null; then
            cd /tmp && git clone https://aur.archlinux.org/yay.git && cd yay && makepkg -si --noconfirm
        fi
        yay -S --noconfirm x86_64-elf-gcc x86_64-elf-binutils
        return 0
    fi
    
    # Fix 3: Check multiboot header
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
    
    # Fix 4: Clean build
    echo "Attempting clean build..."
    make clean
    if make all; then
        echo "✅ Clean build successful"
        
        # Verify the kernel
        if [[ -f kernel.bin ]]; then
            echo "Kernel size: $(stat -c%s kernel.bin) bytes"
            echo "Kernel type: $(file kernel.bin)"
            
            # Check for required symbols
            if x86_64-elf-nm kernel.bin | grep -q "kernel_main"; then
                echo "✅ kernel_main symbol found"
            else
                echo "❌ kernel_main symbol missing"
            fi
        fi
        
        return 0
    fi
    
    # Fix 5: Show detailed error
    echo "❌ Auto-fix failed. Showing detailed build output:"
    make clean
    make all
    
    return 1
}

# Run auto-fix
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
