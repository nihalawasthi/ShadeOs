# ShadeOS

[![GitHub release (latest by date)](https://img.shields.io/github/v/release/nihalawasthi/ShadeOs)](https://github.com/nihalawasthi/ShadeOs/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
![Github Views](https://visitor-badge.laobi.icu/badge?page_id=nihalawasthi.ShadeOs)
![GitHub contributors](https://img.shields.io/github/contributors/nihalawasthi/ShadeOs)
![GitHub issues](https://img.shields.io/github/issues/nihalawasthi/ShadeOs)

<!--![GitHub forks](https://img.shields.io/github/forks/nihalawasthi/ShadeOs)
![GitHub stars](https://img.shields.io/github/stars/nihalawasthi/ShadeOs) -->

A operating system kernel built from scratch, exploring low-level system development with a hybrid C and Rust approach.

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

### Building from Source

1.  **Clone the repository:**
    ```sh
    git clone https://github.com/nihalawasthi/ShadeOs.git
    cd ShadeOs
    ```

2.  **Build the project:**

    You have two alternative methods to build ShadeOS:

    **A) Convenience Script (for Arch Linux)**

    For users on an Arch-based distribution, a convenience script is provided. It will automatically install all required dependencies and then build the project.
    ```sh
    ./install.sh
    ```

    **B) Manual Build (All Systems)**

    On any other system, ensure you have all the [Prerequisites](#prerequisites) installed first. Then, run the following command to build the project:
    ```sh
    make
    ```
    Both methods will compile the C and Rust code and create a bootable `ShadeOS.iso` image in the `bin/` directory.

### Running the OS

After a successful build, you can run ShadeOS in an emulator or a virtual machine.

*   **Using the QEMU Script:** The easiest way to test the OS is with the provided script, which launches QEMU with the correct settings:
    ```sh
    ./run.sh
    ```
*   **Using another Virtual Machine:** Alternatively, you can take the generated `bin/ShadeOS.iso` file and boot it in any standard virtual machine software like VMware or VirtualBox.

## 💿 Downloads

You can download the latest pre-built bootable ISO image from the **GitHub Releases page**.

[**Download Latest Release**](https://github.com/nihalawasthi/ShadeOs/releases/tag/v0.0.1)

## 🤝 Contributing

Contributions are welcome! If you'd like to help improve ShadeOS, please feel free to fork the repository, make your changes, and submit a pull request. For major changes, please open an issue first to discuss what you would like to change.

## 🛡️ Security & Bug Reporting

If you discover a bug or security vulnerability, please open an issue or email me directly at `nihalawasthi498@gmail.com`. Pull requests are also welcome for fixes!

## ✨ Contributors

<br>
<a href="https://github.com/nihalawasthi/ShadeOs/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=nihalawasthi/ShadeOs" />
</a>

## 📜 License

This project is licensed under the MIT License - see the `LICENSE` file for details. (You should add a LICENSE file to your repository).

---