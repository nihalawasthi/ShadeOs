# ShadeOS — User Manual

Quick start (build + run)
1. Install prerequisites (see [scripts/setup.sh](scripts/setup.sh) and [scripts/verify.sh](scripts/verify.sh)).
2. Build everything:
   - Run the default Makefile target:
     make
   - Internals: the Makefile builds C objects and runs:
     cargo build --target x86_64-unknown-none --release --manifest-path kernel-rs/Cargo.toml
3. Create ISO:
   make (produces shadeOS.iso via [Makefile](Makefile))

Run in QEMU (development):
- Use provided script or Makefile target (see [run.sh](run.sh) and [Makefile](Makefile)):
  ./run.sh
- Or with qemu directly:
  qemu-system-x86_64 -m 512 -cdrom shadeOS.iso -serial stdio

Common tasks
- Rebuild Rust library only:
  cd kernel-rs && cargo build --target x86_64-unknown-none --release
- Clean build artifacts:
  make clean

Where to look for outputs / logs
- Kernel boot messages are printed via serial and VGA. The serial implementation is in [kernel-rs/src/serial.rs](kernel-rs/src/serial.rs) and used by many Rust modules (e.g. [`serial_write`](kernel-rs/src/serial.rs)).
- Boot flow starts in [kernel/kernel.c](kernel/kernel.c) and calls Rust entry points such as [`init_heap`](kernel-rs/src/heap.rs) and [`rust_bash_init`](kernel-rs/src/bash.rs).

Troubleshooting
- If build fails due to missing cross-toolchain, run the scripts in [scripts/debug.sh](scripts/debug.sh) or follow [scripts/setup.sh](scripts/setup.sh).