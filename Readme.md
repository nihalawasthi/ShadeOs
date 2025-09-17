# ShadeOS

[![GitHub release (latest by date)](https://img.shields.io/github/v/release/nihalawasthi/ShadeOs)](https://github.com/nihalawasthi/ShadeOs/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
![Github Views](https://visitor-badge.laobi.icu/badge?page_id=nihalawasthi.ShadeOs)
![GitHub contributors](https://img.shields.io/github/contributors/nihalawasthi/ShadeOs)
![GitHub issues](https://img.shields.io/github/issues/nihalawasthi/ShadeOs)

A hobby operating system kernel built from scratch, exploring low-level system development with a hybrid C and Rust approach.

## 🌟 Core Features

ShadeOS has grown from a small project to a functional kernel with a range of advanced features:

*   **Hybrid C & Rust Kernel:** Combines the low-level control of C with the safety and modern features of Rust.
*   **Advanced Memory Management:**
    *   Paging-based virtual memory system.
    *   User/kernel mode separation with ring 0/3 privilege levels.
    *   Copy-on-Write (CoW) for efficient process creation.
*   **Preemptive Multitasking:**
    *   A scheduler for preemptive multitasking and process management.
    *   Support for running processes in user mode with a system call interface.
*   **ELF Binary Support:**
    *   Loads and executes ELF binaries.
    *   Supports dynamic linking and shared libraries.
*   **Modern Filesystem Support:**
    *   Virtual File System (VFS) abstraction layer.
    *   Support for **ext2** and **FAT16** filesystems.
*   **Full TCP/IP Networking Stack:**
    *   Includes TCP, UDP, IPv4, ARP, and ICMP protocols.
    *   A socket API for developing network applications.
    *   Driver for the **RTL8139** network card.
*   **Modular Device Driver Framework:**
    *   PCI bus enumeration and automatic driver binding.
    *   Drivers for keyboard, VGA, RTC, and serial ports.
*   **Interactive Shell:** A command-line shell for interacting with the kernel and running commands.

## 📂 Project Structure

The repository is organized into the following main directories:

*   `kernel/`: Contains the core C components of the kernel, including memory management, networking, and low-level drivers.
*   `kernel-rs/`: Contains the Rust components of the kernel, including the VFS, scheduler, and process management.
*   `boot/`: The assembly code responsible for booting the kernel.
*   `scripts/`: A collection of helper scripts for building, running, and debugging the OS.
*   `Docs/`: Detailed documentation for developers and users.

## 📚 Documentation

For more in-depth information about ShadeOS, please refer to the following documents:

*   **[User Manual](Docs/User_Manual.md):** A guide for using the operating system and its features.
*   **[Developer Guide](Docs/developer_guide.md):** A technical guide for developers who want to contribute to ShadeOS.
*   **[Performance](Docs/performance.md):** Information about the performance of the operating system.

## 🚀 Next Version Highlights

The next version of ShadeOS will focus on the following features:

*   **Full TCP/IP Networking Stack:** 
    *   Includes TCP, UDP, IPv4, ARP, and ICMP protocols.
*   **Modular Device Driver Framework:** 
    *   PCI bus enumeration 
    *   Automatic driver binding.
*   **Inter-Process Communication (IPC):**
    *   Pipes and named pipes (FIFOs).
    *   Shared memory and message queues.
*   **Security & Access Control:**
    *   User authentication and file permissions.
    *   Process capabilities and mandatory access control.

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

[**Download Latest Release**](https://github.com/nihalawasthi/ShadeOs/releases/latest)

## 🛡️ Security & Bug Reporting

If you discover a bug or security vulnerability, please open an issue or email me directly at `nihalawasthi498@gmail.com`. Pull requests are also welcome for fixes!

## ✨ Contributors

<a href="https://github.com/nihalawasthi/ShadeOs/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=nihalawasthi/ShadeOs" />
</a><br>

---
Contributions are welcome! If you'd like to help improve ShadeOS, please feel free to fork the repository, make your changes, and submit a pull request. For major changes, please open an issue first to discuss what you would like to change.

## 📜 License

This project is licensed under the MIT License - see the `LICENSE` file for details.

---