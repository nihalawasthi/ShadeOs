# build.sh - Smart build with error handling

set -e

echo "🔨 Building ShadeOS"
echo "=================="

# Clean and build
make clean
if make all -j4; then
    echo "✅ Build successful"
    make clean
    echo "   Kernel: $(stat -c%s kernel.bin) bytes"
    if [[ -f shadeOS.iso ]]; then
        echo "   ISO: $(stat -c%s shadeOS.iso) bytes"
    fi
    exit 0
else
    echo "❌ Build failed"
    exit 1
fi
