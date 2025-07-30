# ShadeOS

[![GitHub release (latest by date)](https://img.shields.io/github/v/release/nihalawasthi/ShadeOs)](https://github.com/nihalawasthi/ShadeOs/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
<!-- Add other badges like build status once you have CI/CD setup -->

A hobby operating system kernel built from scratch, exploring low-level system development with a hybrid C and Rust approach.

## 🌟 Features

ShadeOS is in its early stages, but it already includes a number of core operating system features:

*   **Hybrid Kernel:** Core components written in C with newer features like the VFS being implemented in Rust.
*   **Memory Management:** A paging-based memory management system.
*   **Interrupts:** A robust interrupt handling system with an Interrupt Descriptor Table (IDT).
*   **I/O System:** Basic support for keyboard input and VGA text mode output.
*   **File System:** A Virtual File System (VFS) abstraction layer with an initial implementation of FAT16.
*   **Multitasking:** Pre-emptive multitasking and process management.
*   **User Space:** Support for running processes in user mode with a system call interface.
*   **Shell:** A basic, interactive command-line shell to interact with the kernel.

## 🚀 Getting Started

### Prerequisites

You will need the following tools to build and run ShadeOS:
*   `make`
*   `gcc` (as part of a cross-compiler toolchain, e.g., `i686-elf-gcc`)
*   `nasm`
*   `ld`
*   `qemu`
*   A Rust toolchain (`rustc`, `cargo`) with the appropriate target.

*(You might want to add more specific details about setting up the cross-compiler and Rust target here.)*

### Building from Source

1.  **Clone the repository:**
    ```sh
    git clone https://github.com/nihalawasthi/ShadeOs.git
    cd ShadeOs
    ```

2.  **Build the project:**
    ```sh
    make all
    ```
    This will compile the C and Rust code and create a bootable ISO image in the `bin/` directory.

### Running with QEMU

To run ShadeOS in an emulator after building it:
```sh
make run
```
This will launch QEMU with the generated ISO file.

## 💿 Downloads

You can download the latest pre-built bootable ISO image from the **GitHub Releases page**.

**Latest Release:** ShadeOS v0.0.1
*(Note: This link is a placeholder. See the guide below on how to create releases and get a real link.)*

## 🤝 Contributing

Contributions are welcome! If you'd like to help improve ShadeOS, please feel free to fork the repository, make your changes, and submit a pull request. For major changes, please open an issue first to discuss what you would like to change.

## 📜 License

This project is licensed under the MIT License - see the `LICENSE` file for details. (You should add a LICENSE file to your repository).

---