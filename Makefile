CC = x86_64-elf-gcc
NASM = nasm
LD = x86_64-elf-ld

CFLAGS = -ffreestanding -fno-pie -nostdlib -mno-red-zone -Wall -Wextra -std=c11 -O2 -Ikernel
ASFLAGS = -f elf64

KERNEL_SOURCES = kernel/kernel.c kernel/vga.c kernel/gdt.c kernel/idt.c kernel/memory.c kernel/multiboot.c kernel/pmm.c kernel/paging.c kernel/heap.c kernel/timer.c kernel/keyboard.c kernel/serial.c kernel/vfs.c kernel/shell.c kernel/pkg.c kernel/rtl8139.c kernel/net.c kernel/task.c kernel/syscall.c
KERNEL_OBJECTS = $(KERNEL_SOURCES:.c=.o)

.PHONY: all clean run debug

all: shadeOS.iso

boot.o: boot/loader.asm
	$(NASM) $(ASFLAGS) boot/loader.asm -o boot.o

gdt_asm.o: kernel/gdt.asm
	$(NASM) $(ASFLAGS) kernel/gdt.asm -o gdt_asm.o

idt_asm.o: kernel/idt.asm
	$(NASM) $(ASFLAGS) kernel/idt.asm -o idt_asm.o

%.o: %.c kernel/kernel.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel.bin: linker.ld boot.o gdt_asm.o idt_asm.o $(addprefix kernel/,kernel.o vga.o gdt.o idt.o memory.o multiboot.o pmm.o paging.o heap.o timer.o keyboard.o serial.o vfs.o shell.o pkg.o rtl8139.o net.o task.o syscall.o fat.o syscall_entry.o)
	$(LD) -T linker.ld -o kernel.bin boot.o gdt_asm.o idt_asm.o kernel/kernel.o kernel/vga.o kernel/gdt.o kernel/idt.o kernel/memory.o kernel/multiboot.o kernel/pmm.o kernel/paging.o kernel/heap.o kernel/timer.o kernel/keyboard.o kernel/serial.o kernel/vfs.o kernel/shell.o kernel/pkg.o kernel/rtl8139.o kernel/net.o kernel/task.o kernel/syscall.o kernel/fat.o kernel/syscall_entry.o

shadeOS.iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o shadeOS.iso iso

clean:
	rm -f *.o kernel/*.o kernel.bin shadeOS.iso
	rm -rf iso

run: shadeOS.iso
	./scripts/run-qemu.sh

debug: shadeOS.iso
	qemu-system-x86_64 -cdrom shadeOS.iso -m 512M -s -S

kernel/fat.o: kernel/fat.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/syscall_entry.o: kernel/syscall_entry.asm
	nasm -f elf64 $< -o $@
