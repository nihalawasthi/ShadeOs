#!/bin/zsh
# setup.sh - Essential environment setup and verification

set -e

echo "🚀 ShadeOS Environment Setup"
echo "============================"

# Install essential packages
sudo pacman -S --needed --noconfirm \
    base-devel git nasm qemu-full grub xorriso libisoburn mtools gdb

# Install cross-compiler if missing
if ! command -v x86_64-elf-gcc &> /dev/null; then
    if ! command -v yay &> /dev/null; then
        cd /tmp && git clone https://aur.archlinux.org/yay.git && cd yay && makepkg -si --noconfirm
    fi
    yay -S --noconfirm x86_64-elf-gcc x86_64-elf-binutils
fi

# Setup aliases
cat >> ~/.zshrc << 'EOF'
alias shade-build='make clean && make'
alias shade-run='make run'
alias shade-debug='make debug'
EOF

echo ""
echo "🔍 Verifying Environment"
echo "========================"

ERRORS=0
check_tool() {
    if command -v "$1" &> /dev/null; then
        echo "✅ $1"
    else
        echo "❌ $1 missing"
        ((ERRORS++))
    fi
}

check_tool "x86_64-elf-gcc"
check_tool "nasm"
check_tool "qemu-system-x86_64"
check_tool "grub-mkrescue"
check_tool "xorriso"

echo ""
if [[ $ERRORS -eq 0 ]]; then
    echo "✅ All tools available"
    echo "✅ Setup complete!"
    exit 0
else
    echo "❌ $ERRORS missing tools"
    exit 1
fi
