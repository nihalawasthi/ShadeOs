#!/usr/bin/env bash
set -e

echo "🚀 ShadeOS Environment Setup"
echo "============================"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "${SCRIPT_DIR}/os-detect.sh" ]; then
    # shellcheck source=/dev/null
    . "${SCRIPT_DIR}/os-detect.sh"
else
    echo "[ERROR] os-detect.sh not found in ${SCRIPT_DIR}"
    exit 1
fi

echo "[INFO] Detected OS family: ${OS_FAMILY}"

if [ "${OS_FAMILY}" = "arch" ]; then
    echo "[INFO] Installing packages with pacman..."
    eval "${PKG_INSTALL_CMD} ${REQUIRED_PACKAGES[*]}"
    # cross compiler via AUR if needed
    if ! command -v x86_64-elf-gcc &> /dev/null; then
        echo "[INFO] Installing x86_64-elf-* from AUR (requires yay)..."
        if ! command -v yay &> /dev/null; then
            cd /tmp && git clone https://aur.archlinux.org/yay.git && cd yay && makepkg -si --noconfirm
        fi
        yay -S --noconfirm x86_64-elf-gcc x86_64-elf-binutils || true
    fi

elif [ "${OS_FAMILY}" = "ubuntu" ]; then
    echo "[INFO] Installing packages with apt..."
    eval "${PKG_UPDATE_CMD}"
    eval "${PKG_INSTALL_CMD} ${REQUIRED_PACKAGES[*]}"
    if ! command -v x86_64-elf-gcc &> /dev/null; then
        echo "[INFO] x86_64-elf-gcc not found. Install or build a cross toolchain if needed."
    fi

else
    echo "[WARNING] Unknown distribution. Please install these packages manually:"
    for p in "${REQUIRED_PACKAGES[@]}"; do echo "  - $p"; done
fi

# Rust toolchain (common)
if ! command -v rustup &> /dev/null; then
    echo "[INFO] Installing rustup..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    source "$HOME/.cargo/env" || true
fi
rustup default nightly || true
rustup target add x86_64-unknown-none || true

# Setup aliases (append if not present)
grep -qxF "alias shade-build='make clean && make'" ~/.zshrc 2>/dev/null || cat >> ~/.zshrc <<'EOF'
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
if [ "${OS_FAMILY}" = "arch" ]; then
check_tool "x86_64-elf-gcc" 
elif [ "${OS_FAMILY}" = "ubuntu" ]; then
check_tool "x86_64-linux-gnu-gcc"
else
    # Try to find any prefixed gcc
    echo "[WARNING]"
fi  

check_tool "make"
check_tool "nasm"
check_tool "qemu-system-x86_64"
check_tool "grub-mkrescue"
check_tool "xorriso"
check_tool "cargo"
check_tool "rustc"

echo ""
if [[ $ERRORS -eq 0 ]]; then
    echo "✅ All tools available"
    echo "✅ Setup complete!"
    exit 0
else
    echo "❌ $ERRORS missing tools — run ./scripts/verify.sh for distro-specific install suggestions"
    exit 1
fi