#![allow(dead_code)]

extern "C" {
    fn rust_paging_new_pml4() -> u64;
    fn rust_paging_free_pml4(pml4_phys: u64);
    fn rust_map_page(pml4_phys: u64, virt: u64, phys: u64, flags: u64);
}

pub fn test_vm_ffi() {
    unsafe {
        let pml4 = rust_paging_new_pml4();
        if pml4 == 0 {
            crate::vga_print(b"[RUST VM] Failed to create PML4\n\0".as_ptr());
            return;
        }
        crate::vga_print(b"[RUST VM] Created new PML4\n\0".as_ptr());
        // Map a dummy page (e.g., 0x400000 -> 0x200000)
        rust_map_page(pml4, 0x400000, 0x200000, 0x7); // Present|RW|User
        crate::vga_print(b"[RUST VM] Mapped page 0x400000 -> 0x200000\n\0".as_ptr());
        // Free the PML4
        rust_paging_free_pml4(pml4);
        crate::vga_print(b"[RUST VM] Freed PML4\n\0".as_ptr());
    }
} 