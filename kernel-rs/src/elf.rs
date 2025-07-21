use crate::rust_kmalloc;

extern "C" {
    fn vga_print(s: *const u8);
    fn serial_write(s: *const u8);
    fn rust_vfs_read(path_ptr: *const u8, buf_ptr: *mut u8, max_len: i32) -> i32;
    fn enter_user_mode(rsp: u64);
    fn rust_paging_new_pml4() -> u64;
    fn rust_map_page(pml4_phys: u64, virt: u64, phys: u64, flags: u64);
}

// ELF constants
const EI_NIDENT: usize = 16;
const ELFMAG: [u8; 4] = [0x7F, b'E', b'L', b'F'];
const ELFCLASS64: u8 = 2;
const ELFDATA2LSB: u8 = 1;
const EV_CURRENT: u8 = 1;
const ET_EXEC: u16 = 2;
const EM_X86_64: u16 = 62;
const PT_LOAD: u32 = 1;

#[repr(C, packed)]
pub struct Elf64_Ehdr {
    pub e_ident: [u8; EI_NIDENT],
    pub e_type: u16,
    pub e_machine: u16,
    pub e_version: u32,
    pub e_entry: u64,
    pub e_phoff: u64,
    pub e_shoff: u64,
    pub e_flags: u32,
    pub e_ehsize: u16,
    pub e_phentsize: u16,
    pub e_phnum: u16,
    pub e_shentsize: u16,
    pub e_shnum: u16,
    pub e_shstrndx: u16,
}

#[repr(C, packed)]
pub struct Elf64_Phdr {
    pub p_type: u32,
    pub p_flags: u32,
    pub p_offset: u64,
    pub p_vaddr: u64,
    pub p_paddr: u64,
    pub p_filesz: u64,
    pub p_memsz: u64,
    pub p_align: u64,
}

fn serial_write_hex(val: u64) {
    let mut buf = [0u8; 18];
    buf[0] = b'0'; buf[1] = b'x';
    for i in 0..16 {
        if i < buf.len() - 3 {
            let shift = 60 - i * 4;
            let digit = ((val >> shift) & 0xF) as u8;
            buf[2 + i] = if digit < 10 { b'0' + digit } else { b'A' + (digit - 10) };
        }
    }
    buf[17] = 0;
    unsafe { serial_write(buf.as_ptr()); }
}

fn serial_write_dec(mut val: u64) {
    let mut buf = [0u8; 21];
    let mut i = 0;
    if val == 0 {
        buf[i] = b'0';
        i += 1;
    } else {
        let mut tmp = val;
        while tmp > 0 {
            buf[i] = b'0' + (tmp % 10) as u8;
            tmp /= 10;
            i += 1;
        }
        // Reverse
        for j in 0..i/2 {
            if j < buf.len() - 3 {
                let t = buf[j];
                buf[j] = buf[i-1-j];
                buf[i-1-j] = t;
            }
        }
    }
    buf[i] = 0;
    unsafe { serial_write(buf.as_ptr()); }
}

fn switch_to_user_mode(entry: u64, stack_top: u64) {
    // Set up the stack for iretq: SS, RSP, RFLAGS, CS, RIP
    // Use GDT selectors: user data = 0x20, user code = 0x18
    let user_ss: u64 = 0x20;
    let user_cs: u64 = 0x18;
    let user_rflags: u64 = 0x202; // IF=1
    unsafe {
        serial_write(b"[ELF] switch_to_user_mode: entry\0".as_ptr());
        serial_write_hex(entry);
        serial_write(b"\n\0".as_ptr());
        serial_write(b"[ELF] switch_to_user_mode: stack_top\0".as_ptr());
        serial_write_hex(stack_top);
        serial_write(b"\n\0".as_ptr());
        let mut user_stack = stack_top;
        // Push in reverse order for iretq
        user_stack -= 8; *(user_stack as *mut u64) = user_ss;
        user_stack -= 8; *(user_stack as *mut u64) = stack_top;
        user_stack -= 8; *(user_stack as *mut u64) = user_rflags;
        user_stack -= 8; *(user_stack as *mut u64) = user_cs;
        user_stack -= 8; *(user_stack as *mut u64) = entry;
        serial_write(b"[ELF] switch_to_user_mode: about to print switching message\0".as_ptr());
        serial_write(b"[ELF] Switching to user mode...\n\0".as_ptr());
        serial_write(b"[ELF] switch_to_user_mode: about to call enter_user_mode\0".as_ptr());
        enter_user_mode(user_stack);
        serial_write(b"[ELF] switch_to_user_mode: returned from enter_user_mode (should not happen)\0".as_ptr());
    }
}

pub extern "C" fn rust_elf_load(path_ptr: *const u8) -> i32 {
    unsafe {
        serial_write(b"[RUST ELF] rust_elf_load called\n\0".as_ptr());
        serial_write(b"[ELF] rust_elf_load: path_ptr=\0".as_ptr());
        serial_write_hex(path_ptr as u64);
        serial_write(b"\n\0".as_ptr());
    }
    let mut buf = [0u8; 4096];
    unsafe {
        serial_write(b"[ELF] rust_elf_load: buf=\0".as_ptr());
        serial_write_hex(&buf as *const _ as u64);
        serial_write(b"\n\0".as_ptr());
    }
    unsafe { serial_write(b"[ELF] About to call rust_vfs_read\n\0".as_ptr()); }
    let read_len = unsafe { rust_vfs_read(path_ptr, buf.as_mut_ptr(), buf.len() as i32) };
    unsafe { serial_write(b"[ELF] Returned from rust_vfs_read\n\0".as_ptr()); }
    unsafe { serial_write(b"[ELF] rust_vfs_read returned: \0".as_ptr()); }
    serial_write_hex(read_len as u64);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    if read_len < core::mem::size_of::<Elf64_Ehdr>() as i32 {
        unsafe { serial_write(b"[ELF] File too small or read failed\n\0".as_ptr()); }
        return -1;
    }
    if read_len as usize > buf.len() {
        unsafe { serial_write(b"[ELF] File too large for buffer\n\0".as_ptr()); }
        return -20;
    }
    // Parse ELF header
    let ehdr = unsafe { &*(buf.as_ptr() as *const Elf64_Ehdr) };
    // Validate ELF magic
    if ehdr.e_ident[0..4] != ELFMAG {
        unsafe { serial_write(b"[ELF] Invalid magic\n\0".as_ptr()); }
        return -2;
    }
    // Validate class, data, version, type, machine
    if ehdr.e_ident[4] != ELFCLASS64 {
        unsafe { serial_write(b"[ELF] Not 64-bit\n\0".as_ptr()); }
        return -3;
    }
    if ehdr.e_ident[5] != ELFDATA2LSB {
        unsafe { serial_write(b"[ELF] Not LSB\n\0".as_ptr()); }
        return -4;
    }
    if ehdr.e_ident[6] != EV_CURRENT {
        unsafe { serial_write(b"[ELF] Bad version\n\0".as_ptr()); }
        return -5;
    }
    if ehdr.e_type != ET_EXEC {
        unsafe { serial_write(b"[ELF] Not executable\n\0".as_ptr()); }
        return -6;
    }
    if ehdr.e_machine != EM_X86_64 {
        unsafe { serial_write(b"[ELF] Not x86_64\n\0".as_ptr()); }
        return -7;
    }
    unsafe { serial_write(b"[ELF] Header OK\n\0".as_ptr()); }
    unsafe { serial_write(b"[ELF] Entry: \0".as_ptr()); }
    serial_write_hex(ehdr.e_entry);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    // Parse program headers
    let phoff = ehdr.e_phoff as usize;
    let phentsize = ehdr.e_phentsize as usize;
    let phnum = ehdr.e_phnum as usize;
    if phoff + phnum * phentsize > read_len as usize {
        unsafe { serial_write(b"[ELF] Program headers out of bounds\n\0".as_ptr()); }
        return -8;
    }
    unsafe { serial_write(b"trying rust paging new\n\0".as_ptr()); }
    // Create a new user address space (PML4)
    let user_pml4 = unsafe { rust_paging_new_pml4() };
    unsafe { serial_write(b"success rust paging new\n\0".as_ptr()); }
    if user_pml4 == 0 {
        unsafe { serial_write(b"[ELF] Failed to create user PML4\n\0".as_ptr()); }
        return -11;
    }
    unsafe { serial_write(b"[ELF] User PML4: \0".as_ptr()); }
    serial_write_hex(user_pml4);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    // Map PT_LOAD segments at their virtual addresses
    for i in 0..phnum {
        let ph_ptr = unsafe { buf.as_ptr().add(phoff + i * phentsize) as *const Elf64_Phdr };
        let ph = unsafe { &*ph_ptr };
        if ph.p_type == PT_LOAD {
            // Defensive: check segment fits in buffer
            if (ph.p_offset as usize) + (ph.p_filesz as usize) > read_len as usize {
                unsafe { serial_write(b"[ELF] Segment out of bounds\n\0".as_ptr()); }
                return -21;
            }
            // Allocate memory for segment
            let seg_mem = unsafe { rust_kmalloc(ph.p_memsz as usize) };
            if seg_mem.is_null() {
                unsafe { serial_write(b"[ELF] Failed to allocate memory for segment\n\0".as_ptr()); }
                return -9;
            }
            // Copy segment data from ELF buffer
            let src = unsafe { buf.as_ptr().add(ph.p_offset as usize) };
            unsafe {
                core::ptr::copy_nonoverlapping(src, seg_mem, ph.p_filesz as usize);
                // Zero .bss region (memsz > filesz)
                if ph.p_memsz > ph.p_filesz {
                    let bss_ptr = seg_mem.add(ph.p_filesz as usize);
                    core::ptr::write_bytes(bss_ptr, 0, (ph.p_memsz - ph.p_filesz) as usize);
                }
            }
            unsafe { serial_write(b"[ELF] Mapped segment at vaddr: \0".as_ptr()); }
            serial_write_hex(ph.p_vaddr);
            unsafe { serial_write(b" to phys: \0".as_ptr()); }
            serial_write_hex(seg_mem as u64);
            unsafe { serial_write(b" size: \0".as_ptr()); }
            serial_write_hex(ph.p_memsz);
            unsafe { serial_write(b"\n\0".as_ptr()); }
        }
    }
    // After mapping all segments, set up user stack
    let stack_size: usize = 0x8000; // 32KB stack
    let user_stack = unsafe { rust_kmalloc(stack_size) };
    if user_stack.is_null() {
        unsafe { serial_write(b"[ELF] Failed to allocate user stack\n\0".as_ptr()); }
        return -10;
    }
    unsafe { serial_write(b"[ELF] User stack allocated at: \0".as_ptr()); }
    unsafe { serial_write(b"[DEBUG] before serial_write_hex(user_stack)\0".as_ptr()); }
    serial_write_hex(user_stack as u64);
    unsafe { serial_write(b"[DEBUG] after serial_write_hex(user_stack)\0".as_ptr()); }
    serial_write_dec(user_stack as u64);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    unsafe { serial_write(b" Size: \0".as_ptr()); }
    unsafe { serial_write(b"[DEBUG] before serial_write_hex(stack_size)\0".as_ptr()); }
    serial_write_hex(stack_size as u64);
    unsafe { serial_write(b"[DEBUG] after serial_write_hex(stack_size)\0".as_ptr()); }
    serial_write_dec(stack_size as u64);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    unsafe { serial_write(b"[ELF] Ready to jump to entry point: \0".as_ptr()); }
    unsafe { serial_write(b"[DEBUG] before serial_write_hex(entry)\0".as_ptr()); }
    serial_write_hex(ehdr.e_entry);
    unsafe { serial_write(b"[DEBUG] after serial_write_hex(entry)\0".as_ptr()); }
    serial_write_dec(ehdr.e_entry);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    unsafe { serial_write(b" with stack: \0".as_ptr()); }
    unsafe { serial_write(b"[DEBUG] before serial_write_hex(stack_top)\0".as_ptr()); }
    serial_write_hex((user_stack as u64) + stack_size as u64);
    unsafe { serial_write(b"[DEBUG] after serial_write_hex(stack_top)\0".as_ptr()); }
    serial_write_dec((user_stack as u64) + stack_size as u64);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    unsafe { serial_write(b" user_pml4: \0".as_ptr()); }
    unsafe { serial_write(b"[DEBUG] before serial_write_hex(user_pml4)\0".as_ptr()); }
    serial_write_hex(user_pml4);
    unsafe { serial_write(b"[DEBUG] after serial_write_hex(user_pml4)\0".as_ptr()); }
    serial_write_dec(user_pml4);
    unsafe { serial_write(b"\n\0".as_ptr()); }
    unsafe { serial_write(b"[ELF] (User mode transition not yet implemented)\n\0".as_ptr()); }
    // Actually switch to user mode (for now, just print and call stub)
    // switch_to_user_mode(ehdr.e_entry, (user_stack as u64) + stack_size as u64);
    0
}
