# ShadeOS — Developer Guide & Architecture

This guide provides an overview of the ShadeOS architecture, development process, and coding conventions. It is intended for developers who want to contribute to the project.

## 1. High-Level Architecture

ShadeOS uses a hybrid kernel architecture that combines the strengths of both C and Rust:

*   **C Kernel:** The core components of the kernel are written in C. This includes the initial boot process, low-level hardware interaction, and the core networking stack. The C code is located in the `kernel/` directory.

*   **Rust Kernel:** Higher-level kernel subsystems are written in Rust to take advantage of its safety features and modern ecosystem. This includes the virtual file system (VFS), process and memory management, the scheduler, and the ELF loader. The Rust code is located in the `kernel-rs/` directory.

The C and Rust components are compiled separately and then linked together to create the final kernel binary. The `Makefile` in the root directory manages this process.

## 2. Code Style and Conventions

To maintain a consistent and readable codebase, we follow these coding conventions:

*   **C Code:** We follow the Linux kernel coding style. Please refer to the [Linux kernel coding style documentation](https://www.kernel.org/doc/html/v4.10/process/coding-style.html) for more details.

*   **Rust Code:** We follow the official [Rust style guide](https://doc.rust-lang.org/1.0.0/style/index.html). We use `rustfmt` to automatically format the code.

*   **Commit Messages:** We follow the [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) specification for our commit messages.

## 3. Important Components

Here is a list of the most important components of the ShadeOS kernel and where to find them:

### Core

*   **Boot / Low-level:** `boot/loader.asm` and `kernel/kernel.c`
*   **Interrupts:** `kernel/idt.c` and `kernel/idt.asm`
*   **System Calls:** `kernel-rs/src/syscalls.rs` and `kernel/syscall_entry.asm`

### Memory Management

*   **Paging:** `kernel/paging.c`
*   **Heap:** `kernel-rs/src/heap.rs`
*   **Virtual Memory:** `kernel-rs/src/vm.rs`

### Processes & Scheduling

*   **Process Management:** `kernel-rs/src/process.rs`
*   **Scheduler:** `kernel-rs/src/scheduler.rs`
*   **ELF Loader:** `kernel-rs/src/elf.rs`

### Filesystems

*   **Virtual File System (VFS):** `kernel-rs/src/vfs.rs`
*   **ext2 Filesystem:** `kernel/ext2.c`
*   **FAT16 Filesystem:** `kernel/fat.c`

### Networking

*   **Core Networking Stack:** `kernel/net.c`
*   **TCP/IP:** `kernel/tcp.c`, `kernel/udp.c`, `kernel/ip.c`
*   **Socket API:** `kernel/socket.c`
*   **RTL8139 Driver:** `kernel/rtl8139.c`

### Drivers

*   **PCI Bus:** `kernel/pci.c`
*   **VGA / Console:** `kernel-rs/src/vga.rs`
*   **Serial Logging:** `kernel-rs/src/serial.rs`
*   **Keyboard:** `kernel/keyboard.c`

## 4. Building and Debugging

For instructions on how to build and run ShadeOS, please refer to the "Getting Started" section in the [main Readme](../Readme.md).

### Debugging with GDB

You can debug the ShadeOS kernel using GDB. The `run.sh` script starts QEMU with the GDB server enabled on port 1234. To start a debugging session, run the following command in a separate terminal:

```sh
gdb -ex "target remote localhost:1234" -ex "symbol-file kernel/kernel.bin"
```

## 5. How to Contribute

We welcome contributions to ShadeOS! If you would like to contribute, please follow these steps:

1.  **Fork the repository:** Create your own fork of the ShadeOS repository on GitHub.

2.  **Create a new branch:** Create a new branch for your changes.

3.  **Make your changes:** Make your changes to the code, following the coding conventions.

4.  **Submit a pull request:** Submit a pull request to the `main` branch of the ShadeOS repository.

### Reporting Bugs

If you find a bug, please open an issue on the [GitHub issue tracker](https://github.com/nihalawasthi/ShadeOs/issues). Please include as much detail as possible, including the steps to reproduce the bug.
