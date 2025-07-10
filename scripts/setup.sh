#!/bin/zsh
# setup.sh - Essential environment setup

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

echo "✅ Setup complete!"
