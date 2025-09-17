# ShadeOS — Developer Guide & Architecture

High level architecture
- Hybrid kernel: core low-level boot & drivers in C, higher-level subsystems (VFS, shell, process manager, ELF loader, scheduler) in Rust.
- Build glue: [Makefile](Makefile) links C objects with Rust staticlib: $(RUST_LIB) -> kernel.bin.

Important components and where to find them
- Boot / low-level:
  - [kernel/boot/loader.asm](boot/loader.asm) and [kernel/kernel.c](kernel/kernel.c)
- VGA / Console:
  - Rust VGA wrappers: [kernel-rs/src/vga.rs](kernel-rs/src/vga.rs) (`vga_print`)
- Serial logging:
  - [kernel-rs/src/serial.rs](kernel-rs/src/serial.rs) (`serial_write`, `serial_write_hex`, `serial_write_dec`)
- Memory / heap:
  - Rust heap: [kernel-rs/src/heap.rs](kernel-rs/src/heap.rs) (`init_heap`, `rust_kmalloc`, `rust_kfree`)
- VFS:
  - Rust VFS: [kernel-rs/src/vfs.rs](kernel-rs/src/vfs.rs) (`rust_vfs_read`, `rust_vfs_write`, `rust_vfs_init`)
  - C stubs: [kernel/vfs_stubs.c](kernel/vfs_stubs.c)
- ELF loader & dynamic linking:
  - [kernel-rs/src/elf.rs](kernel-rs/src/elf.rs) (`rust_elf_load`, `rust_elf_init_dynamic_linking`, `rust_elf_add_symbol`)
- Processes & scheduling:
  - [kernel-rs/src/process.rs](kernel-rs/src/process.rs) (`rust_process_init`, `rust_process_create`, `rust_process_get_current_pid`)
  - Scheduler: [kernel-rs/src/scheduler.rs](kernel-rs/src/scheduler.rs) (`rust_scheduler_tick`)
- Syscalls:
  - [kernel-rs/src/syscalls.rs](kernel-rs/src/syscalls.rs) (`rust_syscall_handler`, syscall impls)

How to add a new user-space test binary
1. Write ELF payload or user program and add it to the Rust VFS via [`rust_vfs_write`](kernel-rs/src/vfs.rs).
2. Use [`rust_elf_load`](kernel-rs/src/elf.rs) to load and (eventually) switch to user mode.

Development tips
- Use serial logging (calls to [`serial_write`](kernel-rs/src/serial.rs)) for traceable output in QEMU.
- For quick experiments with shell commands, inspect the shell implementation at [kernel-rs/src/bash.rs](kernel-rs/src/bash.rs) and exported entry points [`rust_bash_init`](kernel-rs/src/bash.rs) / [`rust_bash_run`](kernel-rs/src/bash.rs).