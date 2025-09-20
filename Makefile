CC = x86_64-elf-gcc
NASM = nasm
LD = x86_64-elf-ld

CFLAGS = -ffreestanding -fno-pie -nostdlib -mno-red-zone -Wall -Wextra -std=c11 -O2 -Ikernel
ASFLAGS = -f elf64

KERNEL_SOURCES = kernel/kernel.c kernel/vga.c kernel/gdt.c kernel/idt.c kernel/memory.c kernel/multiboot.c kernel/pmm.c kernel/paging.c kernel/heap.c kernel/timer.c kernel/rtc.c kernel/keyboard.c kernel/serial.c kernel/pkg.c kernel/rtl8139.c kernel/net.c kernel/device.c kernel/netdev.c kernel/arp.c kernel/icmp.c kernel/tcp.c kernel/http.c kernel/socket.c kernel/task.c kernel/syscall.c kernel/fat.c kernel/ext2.c kernel/blockdev.c kernel/helpers.c kernel/pci.c
KERNEL_OBJECTS = $(KERNEL_SOURCES:.c=.o) kernel/vfs_stubs.o

# Rust specific variables
RUST_TARGET = x86_64-unknown-none
RUST_PROFILE = release
RUST_LIB_DIR = kernel-rs/target/$(RUST_TARGET)/$(RUST_PROFILE)
RUST_LIB = $(RUST_LIB_DIR)/libkernel_rs.a

.PHONY: all clean run debug

all: shadeOS.iso

boot.o: boot/loader.asm
	$(NASM) $(ASFLAGS) boot/loader.asm -o boot.o

gdt_asm.o: kernel/gdt.asm
	$(NASM) $(ASFLAGS) kernel/gdt.asm -o gdt_asm.o

idt_asm.o: kernel/idt.asm
	$(NASM) $(ASFLAGS) kernel/idt.asm -o idt_asm.o

syscall_entry.o: kernel/syscall_entry.asm
	$(NASM) $(ASFLAGS) kernel/syscall_entry.asm -o syscall_entry.o

%.o: %.c kernel/kernel.h
	$(CC) $(CFLAGS) -c $< -o $@

# New rule to build the Rust library
$(RUST_LIB):
	@echo "Building Rust kernel-rs..."
	@mkdir -p $(RUST_LIB_DIR)
	@mkdir -p kernel-rs
	@if [ ! -f kernel-rs/Cargo.toml ]; then \
		echo "Creating new Rust project in kernel-rs/"; \
	fi
	@echo '[package]' > kernel-rs/Cargo.toml
	@echo 'name = "kernel-rs"' >> kernel-rs/Cargo.toml
	@echo 'version = "0.1.0"' >> kernel-rs/Cargo.toml
	@echo 'edition = "2021"' >> kernel-rs/Cargo.toml
	@echo '' >> kernel-rs/Cargo.toml
	@echo '[lib]' >> kernel-rs/Cargo.toml
	@echo 'crate-type = ["staticlib"]' >> kernel-rs/Cargo.toml
	@echo '' >> kernel-rs/Cargo.toml
	@echo '[profile.dev]' >> kernel-rs/Cargo.toml
	@echo 'panic = "abort"' >> kernel-rs/Cargo.toml
	@echo 'lto = true' >> kernel-rs/Cargo.toml
	@echo 'opt-level = "z"' >> kernel-rs/Cargo.toml
	@echo 'codegen-units = 1' >> kernel-rs/Cargo.toml
	@echo '' >> kernel-rs/Cargo.toml
	@echo '[profile.release]' >> kernel-rs/Cargo.toml
	@echo 'panic = "abort"' >> kernel-rs/Cargo.toml
	@echo 'lto = true' >> kernel-rs/Cargo.toml
	@echo 'opt-level = "z"' >> kernel-rs/Cargo.toml
	@echo 'codegen-units = 1' >> kernel-rs/Cargo.toml
	@echo '' >> kernel-rs/Cargo.toml
	@echo '[dependencies]' >> kernel-rs/Cargo.toml
	@echo 'spin = "0.9.8"' >> kernel-rs/Cargo.toml
	@mkdir -p kernel-rs/src
	@if [ ! -f kernel-rs/src/lib.rs ]; then \
		cp kernel-rs/src/lib.rs.template kernel-rs/src/lib.rs; \
	fi
	cargo build --target $(RUST_TARGET) --$(RUST_PROFILE) --manifest-path kernel-rs/Cargo.toml

kernel.bin: linker.ld boot.o gdt_asm.o idt_asm.o syscall_entry.o $(KERNEL_OBJECTS) $(RUST_LIB)
	@echo "RUST_TARGET: $(RUST_TARGET)"
	@echo "RUST_PROFILE: $(RUST_PROFILE)"
	@echo "RUST_LIB_DIR: $(RUST_LIB_DIR)"
	@echo "KERNEL_OBJECTS: $(KERNEL_OBJECTS)"
	@echo "RUST_LIB: $(RUST_LIB)"
	@echo $(LD) -T linker.ld -o kernel.bin boot.o gdt_asm.o idt_asm.o syscall_entry.o $(KERNEL_OBJECTS) $(RUST_LIB) -L$(RUST_LIB_DIR) -lkernel_rs
	$(LD) -T linker.ld -o kernel.bin boot.o gdt_asm.o idt_asm.o syscall_entry.o $(KERNEL_OBJECTS) $(RUST_LIB) -L$(RUST_LIB_DIR) -lkernel_rs

shadeOS.iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o shadeOS.iso iso

clean:
	rm -f *.o kernel/*.o kernel.bin shadeOS.iso
	rm -rf iso
	rm -rf kernel-rs/target
	rm -f kernel-rs/Cargo.toml kernel-rs/src/lib.rs
