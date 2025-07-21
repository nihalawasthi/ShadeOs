Implement process isolation and user/kernel mode separation (ring 0/ring 3), including system call interface and privilege checks.
Done
Add a fully-featured virtual memory subsystem: support for paging, page tables, page faults, copy-on-write, and memory-mapped files.
Done
Implement a robust multitasking scheduler: support for preemptive multitasking, process priorities, and context switching.
Done
Add support for loadable ELF binaries and dynamic linking (execve, shared libraries).
Pending
Implement a complete POSIX-compliant file system (e.g., ext2/ext4, permissions, symlinks, journaling).
Done
Add device driver infrastructure: support for block, character, and network devices, with a driver model and hotplug support.
Pending
Implement networking stack: TCP/IP, UDP, sockets, routing, ARP, DHCP, DNS, etc.
Pending
Add user and group management: UID/GID, permissions, authentication, PAM support.
Pending
Implement signals and inter-process communication (IPC): pipes, message queues, shared memory, semaphores, futexes.
Pending
Add kernel modules and loadable drivers (insmod/rmmod/modprobe).
Pending
Implement power management: ACPI, suspend/resume, CPU frequency scaling.
Pending
Add security features: SELinux/AppArmor, seccomp, namespaces, cgroups, capabilities.
Pending
Implement a complete init system and userland (init, systemd, shell, daemons).
Pending
Add support for advanced hardware: SMP (multi-core), PCI/PCIe, USB, GPU, sound, etc.
Pending
Implement kernel debugging and tracing infrastructure: printk, dmesg, kprobes, ftrace, perf.
Pending
Add support for containers and virtualization: namespaces, cgroups, KVM, LXC, Docker.
Pending


okay so help me resolve kernel crash which is happening right after "[BOOT] Starting Bash shell." i deduced that error might be with either serial_write or vga_print as at some places vga_print was causing crashes and crashes were gone as soon as i commented vga_prints
but here serial_write is the only way to check if the code worked or not so cant comment it out
i also cant find any error as the point where crash is occuring doesnt even has a vga_print and serial write issue doesnt seem to be problem still check it thourogly