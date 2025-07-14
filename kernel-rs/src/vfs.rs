// External C functions for logging
extern "C" {
    fn vga_print(s: *const u8);
    fn serial_write(s: *const u8);
}

#[derive(Copy, Clone)]
#[repr(C)]
pub struct VfsNode {
    used: u8,
    node_type: u8,
    name: [u8; 32],
    size: u32,
    // Add more fields as needed
}

const MAX_FILES: usize = 32;
static mut NODES: [VfsNode; MAX_FILES] = [VfsNode {
    used: 0,
    node_type: 0,
    name: [0; 32],
    size: 0,
}; MAX_FILES];

// Helper function to safely convert C string to Rust string
unsafe fn c_str_to_rust(path_ptr: *const u8) -> &'static str {
    if path_ptr.is_null() {
        return "";
    }
    
    let mut len = 0;
    let mut ptr = path_ptr;
    while *ptr != 0 {
        len += 1;
        ptr = ptr.add(1);
        if len > 256 { // Safety limit
            break;
        }
    }
    
    if len == 0 {
        return "";
    }
    
    // Create a slice from the C string
    let slice = core::slice::from_raw_parts(path_ptr, len);
    core::str::from_utf8(slice).unwrap_or("")
}

#[no_mangle]
pub extern "C" fn rust_vfs_init() {
    unsafe {
        serial_write(b"[RUST VFS] rust_vfs_init called\n\0".as_ptr());
        for i in 0..MAX_FILES {
            NODES[i].used = 0;
            NODES[i].node_type = 0;
            NODES[i].name = [0; 32];
            NODES[i].size = 0;
        }
        NODES[0].used = 1;
        NODES[0].node_type = 1; // Directory
        NODES[0].name[0] = b'/';
        NODES[0].size = 0;
        serial_write(b"[RUST VFS] Node array initialized, root set\n\0".as_ptr());
        vga_print(b"[RUST VFS] Node array initialized, root set\n\0".as_ptr());
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_mkdir(path_ptr: *const u8) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        vga_print(path_ptr);
        vga_print(b"\n\0".as_ptr());
        serial_write(b"[RUST VFS] mkdir called\n\0".as_ptr());
    }
    // Stub: always succeed
    0
}

#[no_mangle]
pub extern "C" fn rust_vfs_ls(path_ptr: *const u8) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        vga_print(path_ptr);
        vga_print(b"\n\0".as_ptr());
        serial_write(b"[RUST VFS] ls called\n\0".as_ptr());
        
        // For now, just print a dummy directory listing
        vga_print(b"  .\n\0".as_ptr());
        vga_print(b"  ..\n\0".as_ptr());
    }
    // Stub: always succeed
    0
}

#[no_mangle]
pub extern "C" fn rust_vfs_read(path_ptr: *const u8, buf_ptr: *mut u8, max_len: i32) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        vga_print(path_ptr);
        vga_print(b"\n\0".as_ptr());
        serial_write(b"[RUST VFS] read called\n\0".as_ptr());
        
        // For now, just return a dummy message
        if !buf_ptr.is_null() && max_len > 0 {
            let dummy_msg = b"File content placeholder\n";
            let copy_len = core::cmp::min(max_len as usize, dummy_msg.len());
            core::ptr::copy_nonoverlapping(dummy_msg.as_ptr(), buf_ptr, copy_len);
            return copy_len as i32;
        }
    }
    // Stub: return error
    -1
}

#[no_mangle]
pub extern "C" fn rust_vfs_write(path_ptr: *const u8, buf_ptr: *const u8, len: i32) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        vga_print(path_ptr);
        vga_print(b"\n\0".as_ptr());
        serial_write(b"[RUST VFS] write called\n\0".as_ptr());
    }
    // Stub: always succeed (return bytes written)
    len
} 