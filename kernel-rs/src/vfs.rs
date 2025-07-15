use core::option::Option;
use core::option::Option::{Some, None};

// External C functions for logging
extern "C" {
    fn vga_print(s: *const u8);
    fn serial_write(s: *const u8);
}

#[derive(Copy, Clone, PartialEq, Eq)]
#[repr(u8)]
pub enum VfsNodeType {
    Unused = 0,
    Dir = 1,
    File = 2,
}

#[derive(Copy, Clone)]
#[repr(C)]
pub struct VfsNode {
    pub used: u8,
    pub node_type: u8,
    pub name: [u8; 32],
    pub size: u32,
    pub parent: *mut VfsNode,
    pub child: *mut VfsNode,
    pub sibling: *mut VfsNode,
}

const MAX_FILES: usize = 32;
static mut NODES: [VfsNode; MAX_FILES] = [VfsNode {
    used: 0,
    node_type: 0,
    name: [0; 32],
    size: 0,
    parent: core::ptr::null_mut(),
    child: core::ptr::null_mut(),
    sibling: core::ptr::null_mut(),
}; MAX_FILES];

const ENOENT: i32 = -2;
const EEXIST: i32 = -17;
const EINVAL: i32 = -22;
const ENOSPC: i32 = -28;

static mut ROOT_PTR: *mut VfsNode = core::ptr::null_mut();

// Helper: Find a free node slot
unsafe fn alloc_node() -> Option<&'static mut VfsNode> {
    for n in NODES.iter_mut() {
        if n.used == 0 {
            n.used = 1;
            return Some(n);
        }
    }
    None
}

// Helper: Compare node name
fn name_eq(a: &[u8], b: &[u8]) -> bool {
    let a_end = a.iter().position(|&c| c == 0).unwrap_or(a.len());
    let b_end = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    &a[..a_end] == &b[..b_end]
}

// Helper: Find child by name
unsafe fn find_child(parent: *mut VfsNode, name: &[u8]) -> Option<*mut VfsNode> {
    if parent.is_null() { return None; }
    let mut child = (*parent).child;
    while !child.is_null() {
        if name_eq(&(*child).name, name) {
            return Some(child);
        }
        child = (*child).sibling;
    }
    None
}

// Helper: Parse path and walk tree
unsafe fn walk_path(path: &str) -> Option<*mut VfsNode> {
    if path == "/" { return Some(ROOT_PTR); }
    let mut node = ROOT_PTR;
    let mut parts = path.trim_start_matches('/').split('/');
    while let Some(part) = parts.next() {
        if part.is_empty() { continue; }
        let mut name = [0u8; 32];
        let bytes = part.as_bytes();
        let len = bytes.len().min(31);
        name[..len].copy_from_slice(&bytes[..len]);
        name[len] = 0;
        match find_child(node, &name) {
            Some(child) => node = child,
            None => return None,
        }
    }
    Some(node)
}

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
pub extern "C" fn rust_vfs_get_root() -> *mut VfsNode {
    unsafe {
        if ROOT_PTR.is_null() {
            return core::ptr::null_mut();
        }
        ROOT_PTR
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_init() -> i32 {
    unsafe {
        serial_write(b"[RUST VFS] rust_vfs_init called\n\0".as_ptr());
        for i in 0..MAX_FILES {
            NODES[i].used = 0;
            NODES[i].node_type = 0;
            NODES[i].name = [0; 32];
            NODES[i].size = 0;
            NODES[i].parent = core::ptr::null_mut();
            NODES[i].child = core::ptr::null_mut();
            NODES[i].sibling = core::ptr::null_mut();
        }
        NODES[0].used = 1;
        NODES[0].node_type = 1; // Directory
        NODES[0].name[0] = b'/';
        NODES[0].size = 0;
        NODES[0].parent = core::ptr::null_mut();
        NODES[0].child = core::ptr::null_mut();
        NODES[0].sibling = core::ptr::null_mut();
        ROOT_PTR = &mut NODES[0];
        serial_write(b"[RUST VFS] Node array initialized, root set\n\0".as_ptr());
        vga_print(b"[RUST VFS] Node array initialized, root set\n\0".as_ptr());
    }
    0
}

#[no_mangle]
pub extern "C" fn rust_vfs_mkdir(path_ptr: *const u8) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        if path.is_empty() || path == "/" { return EEXIST; }
        let (parent_path, new_name) = match path.rfind('/') {
            Some(idx) if idx > 0 => (&path[..idx], &path[idx+1..]),
            Some(_) => ("/", &path[1..]),
            None => ("/", path),
        };
        let parent = walk_path(parent_path).unwrap_or(core::ptr::null_mut());
        if parent.is_null() { return ENOENT; }
        let mut name = [0u8; 32];
        let bytes = new_name.as_bytes();
        let len = bytes.len().min(31);
        name[..len].copy_from_slice(&bytes[..len]);
        name[len] = 0;
        if find_child(parent, &name).is_some() { return EEXIST; }
        let node = match alloc_node() {
            Some(n) => n,
            None => return ENOSPC,
        };
        node.node_type = VfsNodeType::Dir as u8;
        node.size = 0;
        node.parent = parent;
        node.child = core::ptr::null_mut();
        node.sibling = (*parent).child;
        node.name = name;
        (*parent).child = node as *mut VfsNode;
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_ls(path_ptr: *const u8) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        let node = walk_path(path).unwrap_or(core::ptr::null_mut());
        if node.is_null() { return ENOENT; }
        if (*node).node_type != VfsNodeType::Dir as u8 { return EINVAL; }
        vga_print(b"  .\n\0".as_ptr());
        vga_print(b"  ..\n\0".as_ptr());
        let mut child = (*node).child;
        while !child.is_null() {
            let name = &(*child).name;
            let mut buf = [0u8; 34];
            buf[..2].copy_from_slice(b"  ");
            let mut i = 2;
            for &c in name.iter() {
                if c == 0 { break; }
                buf[i] = c;
                i += 1;
            }
            buf[i] = b'\n';
            buf[i+1] = 0;
            vga_print(buf.as_ptr());
            child = (*child).sibling;
        }
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_create_file(path_ptr: *const u8) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        if path.is_empty() || path == "/" { return EEXIST; }
        let (parent_path, new_name) = match path.rfind('/') {
            Some(idx) if idx > 0 => (&path[..idx], &path[idx+1..]),
            Some(_) => ("/", &path[1..]),
            None => ("/", path),
        };
        let parent = walk_path(parent_path).unwrap_or(core::ptr::null_mut());
        if parent.is_null() { return ENOENT; }
        let mut name = [0u8; 32];
        let bytes = new_name.as_bytes();
        let len = bytes.len().min(31);
        name[..len].copy_from_slice(&bytes[..len]);
        name[len] = 0;
        if find_child(parent, &name).is_some() { return EEXIST; }
        let node = match alloc_node() {
            Some(n) => n,
            None => return ENOSPC,
        };
        node.node_type = VfsNodeType::File as u8;
        node.size = 0;
        node.parent = parent;
        node.child = core::ptr::null_mut();
        node.sibling = (*parent).child;
        node.name = name;
        (*parent).child = node as *mut VfsNode;
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_unlink(path_ptr: *const u8) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        if path.is_empty() || path == "/" { return EINVAL; }
        let (parent_path, del_name) = match path.rfind('/') {
            Some(idx) if idx > 0 => (&path[..idx], &path[idx+1..]),
            Some(_) => ("/", &path[1..]),
            None => ("/", path),
        };
        let parent = walk_path(parent_path).unwrap_or(core::ptr::null_mut());
        if parent.is_null() { return ENOENT; }
        let mut name = [0u8; 32];
        let bytes = del_name.as_bytes();
        let len = bytes.len().min(31);
        name[..len].copy_from_slice(&bytes[..len]);
        name[len] = 0;
        let mut prev: *mut VfsNode = core::ptr::null_mut();
        let mut child = (*parent).child;
        while !child.is_null() {
            if name_eq(&(*child).name, &name) {
                if prev.is_null() {
                    (*parent).child = (*child).sibling;
                } else {
                    (*prev).sibling = (*child).sibling;
                }
                (*child).used = 0;
                return 0;
            }
            prev = child;
            child = (*child).sibling;
        }
        ENOENT
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_stat(path_ptr: *const u8, stat_out: *mut VfsNode) -> i32 {
    unsafe {
        let path = c_str_to_rust(path_ptr);
        let node = walk_path(path).unwrap_or(core::ptr::null_mut());
        if node.is_null() { return ENOENT; }
        if !stat_out.is_null() {
            *stat_out = *node;
        }
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_read(path_ptr: *const u8, buf_ptr: *mut u8, max_len: i32) -> i32 {
    unsafe {
        let _path = c_str_to_rust(path_ptr);
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
pub extern "C" fn rust_vfs_write(path_ptr: *const u8, _buf_ptr: *const u8, len: i32) -> i32 {
    unsafe {
        let _path = c_str_to_rust(path_ptr);
        vga_print(path_ptr);
        vga_print(b"\n\0".as_ptr());
        serial_write(b"[RUST VFS] write called\n\0".as_ptr());
    }
    // Stub: always succeed (return bytes written)
    len
} 