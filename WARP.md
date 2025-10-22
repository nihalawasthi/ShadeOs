# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

Project architecture (big picture)
This is shadeOs a kernel made from scratch in C + rust hybrid setting, the end goal of this project is to build a complete OS but the short term goal is to build a pre archinstall level shell based kernel from where user can install the whole OS.

- Boot and entry
  - boot/loader.asm is a Multiboot2-compliant 32-bit loader that verifies CPU features, sets up paging, switches to long mode, and jumps to kernel_main with the Multiboot2 info pointer (zero-extended to 64-bit). linker.ld ensures the multiboot header is placed correctly and lays out sections. GRUB boots kernel.bin via grub.cfg.
- Kernel core (C, with x86_64-elf toolchain)
  - Memory: Physical memory manager (pmm), paging, heap; kernel_main parses the Multiboot2 memory map, initializes pmm, paging, and heap.
  - Interrupts and syscalls: GDT (gdt.c), IDT (idt.c), ISR stubs (set via idt), PIC remap, timer, keyboard; syscall entry (syscall_entry.asm) with a C dispatcher (syscall.c). A Rust-side syscall init is also invoked.
  - Devices and PCI: Simple device framework, PCI discovery (pci.c), and the RTL8139 NIC driver (rtl8139.c). The NIC registers as a net device.
  - Networking: Minimal IPv4 stack with ARP, ICMP, UDP, and TCP (net.c and related); Ethernet frame IO flows through netdev abstraction or directly via RTL8139. The kernel sets a default IP (10.0.2.15) suitable for QEMU user networking.
  - Tasks/processes: Basic multitasking (task.c) and scheduling hooks; C-side task switch/yield integrates with Rust-side structures and hooks.
  - VFS and userland bootstrapping: A Rust-implemented VFS is bridged via C stubs (vfs_stubs.c). On boot, the kernel initializes the VFS, creates a minimal filesystem tree (/bin, /etc, etc.), writes a stub ELF for /bin/bash, and starts a Bash-like shell implemented in Rust.
- Rust companion library (kernel-rs)
  - Built as a static library (libkernel_rs.a) and linked into the C kernel. The Makefile generates kernel-rs/Cargo.toml and copies kernel-rs/src/lib.rs.template to kernel-rs/src/lib.rs before building with cargo for target x86_64-unknown-none (release by default).
  - Provides modules used from C via FFI: vfs, heap (allocator bridge to rust_kmalloc/kfree), process, scheduler, keyboard, bash shell, memory, ELF loader/dynamic linking, ext2, vga, syscalls, serial. A Rust entry point performs keyboard init, ELF dynamic linking init, and ext2 init.

Common commands
- Build
  - Clean and build (ISO + kernel):
    - make clean && make all
    - or: ./install.sh
  - Outputs: kernel.bin and shadeOS.iso
  - Clean only: make clean
  - Notes: Building also compiles the Rust staticlib (kernel-rs) via cargo in release for target x86_64-unknown-none. The Makefile generates kernel-rs/Cargo.toml and kernel-rs/src/lib.rs — edit lib.rs.template and Rust modules under kernel-rs/src when developing.

- Run
  - Run in QEMU (prefers ISO, falls back to kernel):
    - ./run.sh
  - Headless/kernel-only quick run (example equivalent):
    - qemu-system-x86_64 -kernel kernel.bin -m 512M -serial stdio -display none

Key development notes
- Toolchain expectations: x86_64-elf cross-compiler, NASM, GRUB tooling, xorriso, QEMU, and Rust nightly with target x86_64-unknown-none. The setup script provisions these on Arch.
- Rust integration: The C kernel calls into the Rust static library for VFS, shell, some syscalls, and ELF/ext2 features. The Rust side relies on a C-provided allocator (rust_kmalloc/free) and exposes FFI used across the kernel.
- Boot assets and layout: GRUB looks for /boot/kernel.bin within the ISO (grub.cfg). The linkage script places the multiboot header within the first 32KB and aligns sections appropriately.
- Networking defaults: The kernel sets IP 10.0.2.15 for use with QEMU’s user networking. For host interaction, typical QEMU gateway is 10.0.2.2.

Points to remember
- always update todo.md after verifying a change has been successfully made / implemented don't do it before verfying that the change is working
- After making major changes always run "make clean && make all" to test for build errors if there are errors fix them and then only end the response