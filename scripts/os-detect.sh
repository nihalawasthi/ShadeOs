#!/usr/bin/env bash
# OS detection helper — exports distro-aware package/install variables

if [ -r /etc/os-release ]; then
    . /etc/os-release
    DISTRO_ID="${ID:-unknown}"
else
    DISTRO_ID="unknown"
fi

case "${DISTRO_ID,,}" in
    arch|manjaro)
        OS_FAMILY="arch"
        PKG_UPDATE_CMD="sudo pacman -Sy"
        PKG_INSTALL_CMD="sudo pacman -S --noconfirm --needed"
        REQUIRED_PACKAGES=(base-devel git nasm qemu-full grub xorriso libisoburn mtools gdb)
        QEMU_BIN_CANDIDATES=(qemu-system-x86_64 qemu)
        ;;
    ubuntu|debian)
        OS_FAMILY="ubuntu"
        PKG_UPDATE_CMD="sudo apt-get update"
        PKG_INSTALL_CMD="sudo apt-get install -y"
        REQUIRED_PACKAGES=(build-essential git nasm qemu-system-x86 grub-pc-bin xorriso mtools curl)
        QEMU_BIN_CANDIDATES=(qemu-system-x86 qemu-system-x86_64 qemu)
        ;;
    *)
        OS_FAMILY="unknown"
        PKG_UPDATE_CMD=""
        PKG_INSTALL_CMD=""
        REQUIRED_PACKAGES=(nasm make gcc qemu-system-x86_64 xorriso mtools)
        QEMU_BIN_CANDIDATES=(qemu-system-x86_64 qemu)
        ;;
esac

QEMU_BIN=""
for c in "${QEMU_BIN_CANDIDATES[@]}"; do
    if command -v "$c" >/dev/null 2>&1; then
        QEMU_BIN="$(command -v "$c")"
        break
    fi
done

export OS_FAMILY PKG_UPDATE_CMD PKG_INSTALL_CMD REQUIRED_PACKAGES QEMU_BIN