# ShadeOS User Manual

Welcome to ShadeOS! This manual will guide you through the basic features and commands of the operating system.

## 1. Getting Started

To run ShadeOS, you can either build it from the source or download a pre-built image.

*   **Building from Source:** For instructions on how to build ShadeOS from the source code, please refer to the "Getting Started" section in the [main Readme](../Readme.md).
*   **Pre-built Images:** You can download the latest pre-built bootable ISO image from the [GitHub Releases page](https://github.com/nihalawasthi/ShadeOs/releases/latest).

Once you have a bootable ISO image, you can run it in a virtual machine like QEMU, VMware, or VirtualBox.

## 2. The ShadeOS Shell

When you boot ShadeOS, you will be greeted by the interactive shell. This is the primary way to interact with the operating system. The shell allows you to run commands, manage files, and view system information.

## 3. Basic Commands

The ShadeOS shell provides a set of basic commands to help you navigate and interact with the system.

*   `help`

    Displays a list of available commands.

*   `ls`

    Lists the files and directories in the current directory. Currently, it lists the contents of the root directory.

*   `ps`

    Displays a list of the currently running processes and their status.

*   `elf-test`

    This command is for developers. It runs a test to verify the dynamic linking of ELF binaries.

*   `ext2-test`

    This command is for developers. It runs a test to verify the functionality of the ext2 filesystem.

## 4. Networking

ShadeOS has a full TCP/IP networking stack, which allows it to connect to other machines and the internet. The networking features are primarily available to developers through the socket API. In the future, we plan to add user-level networking commands.

## 5. Filesystems

ShadeOS supports both the **ext2** and **FAT16** filesystems. You can use the `ls` command to list the files in the root directory. In the future, we plan to add more commands for file and directory management.
