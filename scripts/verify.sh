# verify.sh - Quick environment verification

set -e

ERRORS=0

check_tool() {
    if command -v "$1" &> /dev/null; then
        echo "✅ $1"
    else
        echo "❌ $1 missing"
        ((ERRORS++))
    fi
}

echo "🔍 Verifying Environment"
echo "========================"

check_tool "x86_64-elf-gcc"
check_tool "nasm"
check_tool "qemu-system-x86_64"
check_tool "grub-mkrescue"
check_tool "xorriso"
check_tool "cargo" # Added Rust cargo check
check_tool "rustc" # Added Rust compiler check

if [[ $ERRORS -eq 0 ]]; then
    echo "✅ All tools available"
    exit 0
else
    echo "❌ $ERRORS missing tools"
    exit 1
fi
