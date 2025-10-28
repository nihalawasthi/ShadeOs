# Add multiboot.c to the kernel sources
KERNEL_OBJS = kernel.o vga.o gdt.o idt.o memory.o multiboot.o pmm.o paging.o heap.o timer.o keyboard.o serial.o vfs.o pkg.o \
	device.o \
	task.o syscall.o fat.o ext2.o syscall_entry.o helpers.o 

fat.o: fat.c
	$(CC) $(CFLAGS) -c $< -o $@

ext2.o: ext2.c
	$(CC) $(CFLAGS) -c $< -o $@

helpers.o: helpers.c
	$(CC) $(CFLAGS) -c $< -o $@

syscall_entry.o: syscall_entry.asm
	$(AS) -f elf64 $< -o $@ 

# ELF loader integration (placeholder for future C stubs)
# elf.o: elf.c
# 	$(CC) $(CFLAGS) -c $< -o $@
