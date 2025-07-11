# Add multiboot.c to the kernel sources
KERNEL_OBJS = kernel.o vga.o gdt.o idt.o memory.o multiboot.o pmm.o paging.o heap.o timer.o keyboard.o serial.o vfs.o shell.o pkg.o rtl8139.o net.o task.o syscall.o syscall_entry.o 

syscall_entry.o: syscall_entry.asm
	$(AS) -f elf64 $< -o $@ 