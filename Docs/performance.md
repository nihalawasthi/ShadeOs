# Performance & Benchmark Plan

Goals
- Measure boot time, syscall latency, context-switch latency, VFS read/write throughput, and network throughput (RTL8139).
- Provide repeatable benchmarks and reporting.

Suggested microbenchmarks
1. Boot time
   - Measure time from POST to first serial log: use serial timestamps in [kernel/kernel.c](kernel/kernel.c)
2. Syscall latency
   - Add a minimal user task that loops calling a simple syscall handled by [`rust_syscall_handler`](kernel-rs/src/syscalls.rs) and measure ticks via timer (see `timer_get_ticks` usage in [kernel-rs/src/bash.rs](kernel-rs/src/bash.rs)).
3. Context switch latency
   - Create two user tasks and ping-pong via yield; measure scheduler transition using [`rust_scheduler_tick`](kernel-rs/src/scheduler.rs) and serial prints.
4. VFS throughput
   - Use [`rust_vfs_write`](kernel-rs/src/vfs.rs) and [`rust_vfs_read`](kernel-rs/src/vfs.rs] to write large buffers and measure elapsed ticks.

How to run benchmarks (manual)
- Build and boot the OS in QEMU:
  make
  ./run.sh
- Use serial output to capture timings (QEMU -serial stdio).
- Log results to a host file via redirection.

Automated harness (recommended)
- Add a Rust benchmark module (e.g. kernel-rs/src/benchmarks.rs) that exposes a `rust_run_benchmarks()` C-callable entry which executes tests and prints CSV results to serial.
- Invoke it from boot sequence (only in debug builds) after init steps.

Example measurement helpers (references)
- Timer ticks are exposed/used in [kernel-rs/src/bash.rs](kernel-rs/src/bash.rs) (`timer_get_ticks`).
- Scheduler tick entry is [`rust_scheduler_tick`](kernel-rs/src/scheduler.rs).

Reporting
- Capture serial output, parse CSV-like result lines, and produce graphs (local scripts).
- Record QEMU command-line, build flags, and CPU/memory settings for reproducibility.