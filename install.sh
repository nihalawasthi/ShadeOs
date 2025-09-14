# install.sh - Only handles setup and build, no running

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }

echo "🚀 ShadeOS Installation"
echo "======================"

# Step 1: Make scripts executable
print_status "Setting script permissions..."
chmod +x scripts/*.sh
print_success "Scripts are now executable"

# Step 2: Setup environment
print_status "Step 1/4: Environment Setup"
if ./scripts/setup.sh; then
    print_success "Environment setup completed"
else
    print_error "Setup failed, running debug..."
    if ./scripts/debug.sh; then
        print_success "Debug resolved the issue, retrying setup..."
        ./scripts/setup.sh
    else
        print_error "Setup failed and could not be auto-resolved"
        exit 1
    fi
fi

# Step 3: Verify environment
print_status "Step 2/4: Environment Verification"
if ./scripts/verify.sh; then
    print_success "Environment verified"
else
    print_error "Verification failed, running debug..."
    if ./scripts/debug.sh; then
        print_success "Debug resolved the issue, retrying verification..."
        ./scripts/verify.sh
    else
        print_error "Verification failed and could not be auto-resolved"
        exit 1
    fi
fi

# Step 4: Build kernel
print_status "Step 3/4: Building Kernel"
if ./scripts/build.sh; then
    print_success "Build completed"
else
    print_error "Build failed, running debug..."
    if ./scripts/debug.sh; then
        print_success "Debug resolved the issue, retrying build..."
        ./scripts/build.sh
    else
        print_error "Build failed and could not be auto-resolved"
        echo ""
        print_status "Manual build attempt with verbose output:"
        make clean
        make all
        exit 1
    fi
fi

# Step 5: Test kernel
print_status "Step 4/4: Testing Kernel"
if ./scripts/test.sh; then
    print_success "Kernel test passed"
else
    print_warning "Test failed, but kernel may still work"
fi

# Installation complete
clear
echo ""
print_success "🎉 ShadeOS Installation Complete!"
echo ""
echo "📋 To run ShadeOS:"
echo "  ./run.sh           - Run ShadeOS in QEMU"
echo ""
echo "📁 Generated files:"
echo "  kernel.bin         - Kernel binary"
echo "  shadeOS.iso        - Bootable ISO"
