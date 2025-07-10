CC = x86_64-elf-gcc
NASM = nasm
LD = x86_64-elf-ld

CFLAGS = -ffreestanding -fno-pie -nostdlib -mno-red-zone -Wall -Wextra -std=c11 -O2 -Ikernel
ASFLAGS = -f elf64

KERNEL_SOURCES = kernel/kernel.c kernel/vga.c kernel/gdt.c kernel/idt.c kernel/memory.c
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

kernel.bin: linker.ld boot.o gdt_asm.o idt_asm.o $(KERNEL_OBJECTS)
	$(LD) -T linker.ld -o kernel.bin boot.o gdt_asm.o idt_asm.o $(KERNEL_OBJECTS)

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
