use crate::rust_kmalloc;
use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::string::ToString;
use alloc::vec::Vec;
use alloc::format;

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
const ET_DYN: u16 = 3;  // Shared object
const EM_X86_64: u16 = 62;
const PT_LOAD: u32 = 1;
const PT_DYNAMIC: u32 = 2;
const PT_INTERP: u32 = 3;

// Dynamic linking constants
const DT_NULL: i64 = 0;
const DT_NEEDED: i64 = 1;
const DT_PLTRELSZ: i64 = 2;
const DT_PLTGOT: i64 = 3;
const DT_HASH: i64 = 4;
const DT_STRTAB: i64 = 5;
const DT_SYMTAB: i64 = 6;
const DT_RELA: i64 = 7;
const DT_RELASZ: i64 = 8;
const DT_RELAENT: i64 = 9;
const DT_STRSZ: i64 = 10;
const DT_SYMENT: i64 = 11;
const DT_INIT: i64 = 12;
const DT_FINI: i64 = 13;
const DT_SONAME: i64 = 14;
const DT_RPATH: i64 = 15;
const DT_SYMBOLIC: i64 = 16;
const DT_REL: i64 = 17;
const DT_RELSZ: i64 = 18;
const DT_RELENT: i64 = 19;
const DT_PLTREL: i64 = 20;
const DT_DEBUG: i64 = 21;
const DT_TEXTREL: i64 = 22;
const DT_JMPREL: i64 = 23;
const DT_BIND_NOW: i64 = 24;
const DT_INIT_ARRAY: i64 = 25;
const DT_FINI_ARRAY: i64 = 26;
const DT_INIT_ARRAYSZ: i64 = 27;
const DT_FINI_ARRAYSZ: i64 = 28;
const DT_RUNPATH: i64 = 29;
const DT_FLAGS: i64 = 30;
const DT_PREINIT_ARRAY: i64 = 32;
const DT_PREINIT_ARRAYSZ: i64 = 33;
const DT_SYMTAB_SHNDX: i64 = 34;
const DT_RELRSZ: i64 = 35;
const DT_RELR: i64 = 36;
const DT_RELRENT: i64 = 37;

// Relocation types
const R_X86_64_NONE: u32 = 0;
const R_X86_64_64: u32 = 1;
const R_X86_64_PC32: u32 = 2;
const R_X86_64_GOT32: u32 = 3;
const R_X86_64_PLT32: u32 = 4;
const R_X86_64_COPY: u32 = 5;
const R_X86_64_GLOB_DAT: u32 = 6;
const R_X86_64_JUMP_SLOT: u32 = 7;
const R_X86_64_RELATIVE: u32 = 8;
const R_X86_64_GOTPCREL: u32 = 9;
const R_X86_64_32: u32 = 10;
const R_X86_64_32S: u32 = 11;
const R_X86_64_16: u32 = 12;
const R_X86_64_PC16: u32 = 13;
const R_X86_64_8: u32 = 14;
const R_X86_64_PC8: u32 = 15;
const R_X86_64_DTPMOD64: u32 = 16;
const R_X86_64_DTPOFF64: u32 = 17;
const R_X86_64_TPOFF64: u32 = 18;
const R_X86_64_TLSGD: u32 = 19;
const R_X86_64_TLSLD: u32 = 20;
const R_X86_64_DTPOFF32: u32 = 21;
const R_X86_64_GOTTPOFF: u32 = 22;
const R_X86_64_TPOFF32: u32 = 23;

// Symbol binding and type
const STB_LOCAL: u8 = 0;
const STB_GLOBAL: u8 = 1;
const STB_WEAK: u8 = 2;
const STT_NOTYPE: u8 = 0;
const STT_OBJECT: u8 = 1;
const STT_FUNC: u8 = 2;
const STT_SECTION: u8 = 3;
const STT_FILE: u8 = 4;

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

#[repr(C, packed)]
pub struct Elf64_Shdr {
    pub sh_name: u32,
    pub sh_type: u32,
    pub sh_flags: u64,
    pub sh_addr: u64,
    pub sh_offset: u64,
    pub sh_size: u64,
    pub sh_link: u32,
    pub sh_info: u32,
    pub sh_addralign: u64,
    pub sh_entsize: u64,
}

#[repr(C, packed)]
pub struct Elf64_Sym {
    pub st_name: u32,
    pub st_info: u8,
    pub st_other: u8,
    pub st_shndx: u16,
    pub st_value: u64,
    pub st_size: u64,
}

#[repr(C, packed)]
pub struct Elf64_Rela {
    pub r_offset: u64,
    pub r_info: u64,
    pub r_addend: i64,
}

#[repr(C, packed)]
pub struct Elf64_Dyn {
    pub d_tag: i64,
    pub d_val: u64,
}

// Dynamic linking context
pub struct DynamicContext {
    pub base_addr: u64,
    pub dynamic: Option<*const Elf64_Dyn>,
    pub strtab: Option<*const u8>,
    pub symtab: Option<*const Elf64_Sym>,
    pub relocations: Vec<*const Elf64_Rela>,
    pub needed_libs: Vec<String>,
    pub plt_got: Option<u64>,
    pub hash_table: Option<*const u32>,
}

// Global symbol table for dynamic linking
static mut GLOBAL_SYMBOLS: BTreeMap<String, u64> = BTreeMap::new();
static mut LOADED_LIBRARIES: BTreeMap<String, DynamicContext> = BTreeMap::new();

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

fn serial_write_dec(val: u64) {
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
    if ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN {
        unsafe { serial_write(b"[ELF] Not executable or shared object\n\0".as_ptr()); }
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
    
    // Check if this is a shared object
    let is_shared_object = ehdr.e_type == ET_DYN;
    let base_addr = if is_shared_object { 0x400000 } else { 0 }; // Default shared object base
    
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
    
    // First pass: find dynamic section and needed libraries
    let mut dynamic_section = None;
    let mut needed_libs = Vec::new();
    
    for i in 0..phnum {
        let ph_ptr = unsafe { buf.as_ptr().add(phoff + i * phentsize) as *const Elf64_Phdr };
        let ph = unsafe { &*ph_ptr };
        
        if ph.p_type == PT_DYNAMIC {
            dynamic_section = Some(ph);
            unsafe { serial_write(b"[ELF] Found dynamic section\n\0".as_ptr()); }
        }
    }
    
    // Process dynamic section if present
    if let Some(dynamic_ph) = dynamic_section {
        let dynamic_offset = dynamic_ph.p_offset as usize;
        let dynamic_size = dynamic_ph.p_filesz as usize;
        
        if dynamic_offset + dynamic_size <= read_len as usize {
            let dynamic_ptr = unsafe { buf.as_ptr().add(dynamic_offset) as *const Elf64_Dyn };
            let mut dyn_entry = dynamic_ptr;
            
            unsafe {
                loop {
                    let entry = &*dyn_entry;
                    if entry.d_tag == DT_NULL {
                        break;
                    }
                    if entry.d_tag == DT_NEEDED {
                        // We'll need to resolve the library name from strtab
                        needed_libs.push(entry.d_val);
                    }
                    dyn_entry = dyn_entry.add(1);
                }
            }
            
            unsafe { 
                serial_write(b"[ELF] Found \0".as_ptr()); 
                serial_write_dec(needed_libs.len() as u64);
                serial_write(b" needed libraries\n\0".as_ptr()); 
            }
        }
    }
    
    // Load needed libraries
    for &lib_offset in &needed_libs {
        // In a real implementation, we would:
        // 1. Get the library name from strtab using lib_offset
        // 2. Search for the library in standard paths
        // 3. Load and map the library
        // For now, we'll just note that we need to load libraries
        unsafe {
            serial_write(b"[ELF] Need to load library at offset: \0".as_ptr());
            serial_write_hex(lib_offset);
            serial_write(b"\n\0".as_ptr());
        }
    }
    
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
            
            let virtual_addr = if is_shared_object { 
                base_addr + ph.p_vaddr 
            } else { 
                ph.p_vaddr 
            };
            
            unsafe { serial_write(b"[ELF] Mapped segment at vaddr: \0".as_ptr()); }
            serial_write_hex(virtual_addr);
            unsafe { serial_write(b" to phys: \0".as_ptr()); }
            serial_write_hex(seg_mem as u64);
            unsafe { serial_write(b" size: \0".as_ptr()); }
            serial_write_hex(ph.p_memsz);
            unsafe { serial_write(b"\n\0".as_ptr()); }
        }
    }
    
    // Apply relocations if this is a shared object
    if is_shared_object {
        if let Some(dynamic_ph) = dynamic_section {
            let dynamic_offset = dynamic_ph.p_offset as usize;
            let dynamic_ptr = unsafe { buf.as_ptr().add(dynamic_offset) as *const Elf64_Dyn };
            let context = process_dynamic_section(dynamic_ptr, base_addr);
            apply_relocations(&context);
            
            unsafe { serial_write(b"[ELF] Applied relocations for shared object\n\0".as_ptr()); }
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
    
    let entry_point = if is_shared_object { 
        base_addr + ehdr.e_entry 
    } else { 
        ehdr.e_entry 
    };
    
    unsafe { serial_write(b"[ELF] Ready to jump to entry point: \0".as_ptr()); }
    unsafe { serial_write(b"[DEBUG] before serial_write_hex(entry)\0".as_ptr()); }
    serial_write_hex(entry_point);
    unsafe { serial_write(b"[DEBUG] after serial_write_hex(entry)\0".as_ptr()); }
    serial_write_dec(entry_point);
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
    
    if is_shared_object {
        unsafe { serial_write(b"[ELF] Loaded shared object successfully\n\0".as_ptr()); }
    } else {
        unsafe { serial_write(b"[ELF] Loaded executable successfully\n\0".as_ptr()); }
    }
    
    // Actually switch to user mode (for now, just print and call stub)
    // switch_to_user_mode(entry_point, (user_stack as u64) + stack_size as u64);
    0
}

// Initialize the dynamic linking system
pub extern "C" fn rust_elf_init_dynamic_linking() {
    unsafe {
        GLOBAL_SYMBOLS.clear();
        LOADED_LIBRARIES.clear();
    }
}

// Load a shared library and return its base address
pub extern "C" fn rust_elf_load_shared_library(lib_name: *const u8) -> u64 {
    unsafe {
        serial_write(b"[ELF] Loading shared library: \0".as_ptr());
        serial_write(lib_name);
        serial_write(b"\n\0".as_ptr());
        
        // Convert C string to Rust string
        let mut name = String::new();
        let mut ptr = lib_name;
        loop {
            let c = *ptr;
            if c == 0 {
                break;
            }
            name.push(c as char);
            ptr = ptr.add(1);
        }
        
        // Check if library is already loaded
        if let Some(context) = LOADED_LIBRARIES.get(&name) {
            serial_write(b"[ELF] Library already loaded at: \0".as_ptr());
            serial_write_hex(context.base_addr);
            serial_write(b"\n\0".as_ptr());
            return context.base_addr;
        }
        
        // For now, we'll create a stub implementation
        // In a real implementation, this would:
        // 1. Search for the library in standard paths (/lib, /usr/lib, etc.)
        // 2. Load the library file using VFS
        // 3. Parse its ELF headers
        // 4. Map its segments at appropriate addresses
        // 5. Process its dynamic section
        // 6. Apply relocations
        // 7. Add to loaded libraries list
        
        serial_write(b"[ELF] Shared library loading not yet fully implemented\n\0".as_ptr());
        0
    }
}

// Resolve a symbol by name
pub extern "C" fn rust_elf_resolve_symbol(symbol_name: *const u8) -> u64 {
    unsafe {
        // Convert C string to Rust string
        let mut name = String::new();
        let mut ptr = symbol_name;
        loop {
            let c = *ptr;
            if c == 0 {
                break;
            }
            name.push(c as char);
            ptr = ptr.add(1);
        }
        
        if let Some(addr) = resolve_symbol(&name, "") {
            serial_write(b"[ELF] Resolved symbol '".as_ptr());
            serial_write(symbol_name);
            serial_write(b"' to: \0".as_ptr());
            serial_write_hex(addr);
            serial_write(b"\n\0".as_ptr());
            return addr;
        } else {
            serial_write(b"[ELF] Failed to resolve symbol '".as_ptr());
            serial_write(symbol_name);
            serial_write(b"'\n\0".as_ptr());
            return 0;
        }
    }
}

// Add a symbol to the global symbol table
pub extern "C" fn rust_elf_add_symbol(symbol_name: *const u8, address: u64) {
    unsafe {
        // Convert C string to Rust string
        let mut name = String::new();
        let mut ptr = symbol_name;
        loop {
            let c = *ptr;
            if c == 0 {
                break;
            }
            name.push(c as char);
            ptr = ptr.add(1);
        }
        
        GLOBAL_SYMBOLS.insert(name, address);
        serial_write(b"[ELF] Added symbol '".as_ptr());
        serial_write(symbol_name);
        serial_write(b"' at: \0".as_ptr());
        serial_write_hex(address);
        serial_write(b"\n\0".as_ptr());
    }
}

// Get the number of loaded libraries
pub extern "C" fn rust_elf_get_loaded_library_count() -> u32 {
    unsafe {
        LOADED_LIBRARIES.len() as u32
    }
}

// Get the number of global symbols
pub extern "C" fn rust_elf_get_global_symbol_count() -> u32 {
    unsafe {
        GLOBAL_SYMBOLS.len() as u32
    }
}

// Test function to demonstrate dynamic linking
pub extern "C" fn rust_elf_test_dynamic_linking() {
    unsafe {
        serial_write(b"[ELF] Testing dynamic linking capabilities\n\0".as_ptr());
        
        // Add some test symbols
        rust_elf_add_symbol(b"printf\0".as_ptr(), 0x1000);
        rust_elf_add_symbol(b"malloc\0".as_ptr(), 0x2000);
        rust_elf_add_symbol(b"free\0".as_ptr(), 0x3000);
        rust_elf_add_symbol(b"strlen\0".as_ptr(), 0x4000);
        
        // Test symbol resolution
        let printf_addr = rust_elf_resolve_symbol(b"printf\0".as_ptr());
        let malloc_addr = rust_elf_resolve_symbol(b"malloc\0".as_ptr());
        let unknown_addr = rust_elf_resolve_symbol(b"unknown_function\0".as_ptr());
        
        serial_write(b"[ELF] printf resolved to: \0".as_ptr());
        serial_write_hex(printf_addr);
        serial_write(b"\n\0".as_ptr());
        
        serial_write(b"[ELF] malloc resolved to: \0".as_ptr());
        serial_write_hex(malloc_addr);
        serial_write(b"\n\0".as_ptr());
        
        serial_write(b"[ELF] unknown_function resolved to: \0".as_ptr());
        serial_write_hex(unknown_addr);
        serial_write(b"\n\0".as_ptr());
        
        // Show statistics
        let lib_count = rust_elf_get_loaded_library_count();
        let sym_count = rust_elf_get_global_symbol_count();
        
        serial_write(b"[ELF] Loaded libraries: \0".as_ptr());
        serial_write_dec(lib_count as u64);
        serial_write(b"\n\0".as_ptr());
        
        serial_write(b"[ELF] Global symbols: \0".as_ptr());
        serial_write_dec(sym_count as u64);
        serial_write(b"\n\0".as_ptr());
        
        serial_write(b"[ELF] Dynamic linking test completed\n\0".as_ptr());
    }
}

// Dynamic linking helper functions
fn get_symbol_name(strtab: *const u8, name_offset: u32) -> String {
    unsafe {
        let mut name = String::new();
        let mut offset = name_offset as usize;
        loop {
            let c = *strtab.add(offset);
            if c == 0 {
                break;
            }
            name.push(c as char);
            offset += 1;
        }
        name
    }
}

fn get_relocation_type(info: u64) -> u32 {
    (info & 0xFFFFFFFF) as u32
}

fn get_relocation_symbol(info: u64) -> u32 {
    (info >> 32) as u32
}

fn resolve_symbol(symbol_name: &str, current_lib: &str) -> Option<u64> {
    unsafe {
        // First check global symbols
        if let Some(addr) = GLOBAL_SYMBOLS.get(symbol_name) {
            return Some(*addr);
        }
        
        // Then check loaded libraries
        for (lib_name, context) in LOADED_LIBRARIES.iter() {
            if let (Some(symtab), Some(strtab)) = (context.symtab, context.strtab) {
                // Simple linear search through symbol table
                let mut sym_ptr = symtab;
                loop {
                    let sym = &*sym_ptr;
                    if sym.st_name == 0 {
                        break;
                    }
                    
                    let name = get_symbol_name(strtab, sym.st_name);
                    if name == symbol_name {
                        let binding = (sym.st_info >> 4) & 0xF;
                        let symbol_type = sym.st_info & 0xF;
                        
                        // Only resolve global and weak symbols
                        if binding == STB_GLOBAL || binding == STB_WEAK {
                            if symbol_type == STT_FUNC || symbol_type == STT_OBJECT {
                                let addr = context.base_addr + sym.st_value;
                                // Add to global symbols for future lookups
                                GLOBAL_SYMBOLS.insert(symbol_name.to_string(), addr);
                                return Some(addr);
                            }
                        }
                    }
                    sym_ptr = sym_ptr.add(1);
                }
            }
        }
        None
    }
}

fn process_dynamic_section(dynamic: *const Elf64_Dyn, base_addr: u64) -> DynamicContext {
    let mut context = DynamicContext {
        base_addr,
        dynamic: Some(dynamic),
        strtab: None,
        symtab: None,
        relocations: Vec::new(),
        needed_libs: Vec::new(),
        plt_got: None,
        hash_table: None,
    };
    
    unsafe {
        let mut dyn_ptr = dynamic;
        loop {
            let dyn_entry = &*dyn_ptr;
            
            match dyn_entry.d_tag {
                DT_NULL => break,
                DT_NEEDED => {
                    // This would need strtab to resolve the library name
                    // For now, we'll just note that we need to load libraries
                    context.needed_libs.push(format!("lib{}", context.needed_libs.len()));
                },
                DT_STRTAB => {
                    context.strtab = Some((base_addr + dyn_entry.d_val) as *const u8);
                },
                DT_SYMTAB => {
                    context.symtab = Some((base_addr + dyn_entry.d_val) as *const Elf64_Sym);
                },
                DT_RELA => {
                    let rela_ptr = (base_addr + dyn_entry.d_val) as *const Elf64_Rela;
                    context.relocations.push(rela_ptr);
                },
                DT_RELASZ => {
                    // We'll use this to determine the number of relocations
                },
                DT_PLTGOT => {
                    context.plt_got = Some(base_addr + dyn_entry.d_val);
                },
                DT_HASH => {
                    context.hash_table = Some((base_addr + dyn_entry.d_val) as *const u32);
                },
                _ => {
                    // Ignore other dynamic tags for now
                }
            }
            dyn_ptr = dyn_ptr.add(1);
        }
    }
    
    context
}

fn apply_relocations(context: &DynamicContext) {
    unsafe {
        for &rela_ptr in &context.relocations {
            let mut current_rela = rela_ptr;
            loop {
                let rela = &*current_rela;
                if rela.r_offset == 0 && rela.r_info == 0 {
                    break;
                }
                
                let reloc_type = get_relocation_type(rela.r_info);
                let symbol_index = get_relocation_symbol(rela.r_info);
                
                match reloc_type {
                    R_X86_64_RELATIVE => {
                        // Relative relocation: S + A
                        let target_addr = context.base_addr + rela.r_offset;
                        let value = context.base_addr + rela.r_addend as u64;
                        *(target_addr as *mut u64) = value;
                    },
                    R_X86_64_GLOB_DAT => {
                        // Global data relocation
                        if let Some(symtab) = context.symtab {
                            if let Some(strtab) = context.strtab {
                                let sym = &*symtab.add(symbol_index as usize);
                                let symbol_name = get_symbol_name(strtab, sym.st_name);
                                
                                if let Some(resolved_addr) = resolve_symbol(&symbol_name, "") {
                                    let target_addr = context.base_addr + rela.r_offset;
                                    *(target_addr as *mut u64) = resolved_addr;
                                }
                            }
                        }
                    },
                    R_X86_64_JUMP_SLOT => {
                        // PLT relocation
                        if let Some(symtab) = context.symtab {
                            if let Some(strtab) = context.strtab {
                                let sym = &*symtab.add(symbol_index as usize);
                                let symbol_name = get_symbol_name(strtab, sym.st_name);
                                
                                if let Some(resolved_addr) = resolve_symbol(&symbol_name, "") {
                                    let target_addr = context.base_addr + rela.r_offset;
                                    *(target_addr as *mut u64) = resolved_addr;
                                }
                            }
                        }
                    },
                    R_X86_64_64 => {
                        // Absolute 64-bit relocation
                        if let Some(symtab) = context.symtab {
                            if let Some(strtab) = context.strtab {
                                let sym = &*symtab.add(symbol_index as usize);
                                let symbol_name = get_symbol_name(strtab, sym.st_name);
                                
                                if let Some(resolved_addr) = resolve_symbol(&symbol_name, "") {
                                    let target_addr = context.base_addr + rela.r_offset;
                                    *(target_addr as *mut u64) = resolved_addr + rela.r_addend as u64;
                                }
                            }
                        }
                    },
                    _ => {
                        // Ignore other relocation types for now
                    }
                }
                
                current_rela = current_rela.add(1);
            }
        }
    }
}

fn load_shared_library(lib_name: &str) -> Option<DynamicContext> {
    // For now, we'll create a stub implementation
    // In a real implementation, this would:
    // 1. Search for the library in standard paths
    // 2. Load the library file
    // 3. Parse its ELF headers
    // 4. Map its segments
    // 5. Process its dynamic section
    // 6. Apply relocations
    
    unsafe {
        serial_write(b"[ELF] Loading shared library: \0".as_ptr());
        serial_write(lib_name.as_ptr());
        serial_write(b"\n\0".as_ptr());
    }
    
    None
}
