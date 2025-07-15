#![no_std]
#![no_main] // Disable the default main function

use core::panic::PanicInfo;
use core::ffi::c_void;
use core::marker::{Sync, Send};

// Define C functions that Rust will call
extern "C" {
    fn vga_print(s: *const u8);
    fn serial_write(s: *const u8);
    fn task_schedule(); // Placeholder for scheduler
    fn sys_cli(); // Disable interrupts
    fn sys_sti(); // Enable interrupts
    fn pause();   // HLT instruction
}

mod vfs;
mod heap; // Required for kernel heap FFI
mod memory; // Required for kernel memory FFI
mod keyboard; // New keyboard module

pub use vfs::*; // Export VFS functions
pub use heap::*; // Export heap functions
pub use memory::*; // Export memory functions

// Force emission of FFI functions for C linkage
#[used]
static _KEEP_FFI: [&'static (dyn Sync + Send); 7] = [
    &rust_vfs_mkdir,
    &rust_vfs_ls,
    &rust_vfs_read,
    &rust_vfs_write,
    &rust_vfs_create_file,
    &rust_vfs_unlink,
    &rust_vfs_stat,
];

// A simple panic handler for bare-metal
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe {
        serial_write(b"[RUST PANIC] Kernel panicked!\n\0".as_ptr());
        vga_print(b"[RUST PANIC] Kernel panicked!\n\0".as_ptr());
    }
    loop {}
}

// This function will be called from C
#[no_mangle]
pub extern "C" fn rust_entry_point() {
    unsafe {
        vga_print(b"[RUST] Hello from Rust!\n\0".as_ptr());
        serial_write(b"[RUST] Hello from Rust!\n\0".as_ptr());
    }
    keyboard::init(); // Initialize the Rust keyboard buffer
    // No longer creating a user process here, as it's done in C kernel_main
    // No longer calling task_schedule here, as it's done in C kernel_main
}

// Placeholder for scheduler tick, if needed by C
#[no_mangle]
pub extern "C" fn rust_scheduler_tick() {
    // Implement Rust-side scheduling logic here if needed
    // For now, it's a no-op as C handles task_yield/task_exit
}
