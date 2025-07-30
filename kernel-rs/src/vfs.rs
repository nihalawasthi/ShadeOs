use alloc::collections::BTreeMap;
use alloc::vec::Vec;
use alloc::string::String;
use alloc::string::ToString;
use core::iter::Iterator;
use core::ptr;
use core::clone::Clone;
use core::option::Option;
use core::option::Option::{Some, None};

extern "C" {
    fn serial_write(s: *const u8);
    fn vga_print(s: *const u8);
    fn sys_cli(); // Disable interrupts
    fn sys_sti(); // Enable interrupts
}

#[derive(Clone)]
enum FileType {
    Regular,
    Directory,
}

#[derive(Clone)]
struct FileEntry {
    name: String,
    file_type: FileType,
    data: Vec<u8>,
    children: BTreeMap<String, FileEntry>,
}

impl FileEntry {
    fn new_file(name: String) -> Self {
        let entry = FileEntry {
            name,
            file_type: FileType::Regular,
            data: Vec::new(),
            children: BTreeMap::new(),
        };
        let ptr = &entry as *const _ as usize;
        let mut buf = [b'V', b'F', b'S', b'-', b'A', b'L', b'L', b'O', b'C', b':', b' ', b'0', b'x', b'0', b'0', b'0', b'0', b'0', b'0', b'0', b'0', b'\r', b'\n', 0];
        let mut addr = ptr;
        for i in (11..19).rev() {
            if i < buf.len() - 3 {
                let digit = (addr & 0xF) as u8;
                buf[i] = if digit < 10 { b'0' + digit } else { b'a' + (digit - 10) };
            }
            addr >>= 4;
        }
        unsafe { serial_write(buf.as_ptr()); }
        entry
    }
    
    fn new_directory(name: String) -> Self {
        let entry = FileEntry {
            name,
            file_type: FileType::Directory,
            data: Vec::new(),
            children: BTreeMap::new(),
        };
        let ptr = &entry as *const _ as usize;
        let mut buf = [b'V', b'F', b'S', b'-', b'A', b'L', b'L', b'O', b'C', b':', b' ', b'0', b'x', b'0', b'0', b'0', b'0', b'0', b'0', b'0', b'0', b'\r', b'\n', 0];
        let mut addr = ptr;
        for i in (11..19).rev() {
            if i < buf.len() - 3 {
                let digit = (addr & 0xF) as u8;
                buf[i] = if digit < 10 { b'0' + digit } else { b'a' + (digit - 10) };
            }
            addr >>= 4;
        }
        unsafe { serial_write(buf.as_ptr()); }
        entry
    }
}

static mut ROOT_FS: Option<FileEntry> = None;

pub fn init() {
    unsafe {
        serial_write(b"[VFS] Initializing virtual file system\n\0".as_ptr());
        ROOT_FS = Some(FileEntry::new_directory("/".to_string()));
        serial_write(b"[VFS] Virtual file system initialized\n\0".as_ptr());
    }
}

fn get_path_components(path: *const u8) -> Vec<String> {
    if path.is_null() {
        return Vec::new();
    }
    
    let mut components = Vec::new();
    let mut current = String::new();
    let mut i = 0;
    
    unsafe {
        loop {
            let c = *path.add(i);
            if c == 0 {
                break;
            }
            
            if c == b'/' {
                if !current.is_empty() {
                    components.push(current.clone());
                    current.clear();
                }
            } else {
                current.push(c as char);
            }
            
            i += 1;
        }
        
        if !current.is_empty() {
            components.push(current);
        }
    }
    
    components
}

fn find_entry(path: *const u8) -> Option<&'static mut FileEntry> {
    unsafe {
        if let Some(ref mut root) = ROOT_FS {
            let components = get_path_components(path);
            
            if components.is_empty() {
                return Some(root);
            }
            
            let mut current = root;
            for component in components {
                if let Some(child) = current.children.get_mut(&component) {
                    current = child;
                } else {
                    return None;
                }
            }
            
            Some(current)
        } else {
            None
        }
    }
}

fn find_parent_and_name(path: *const u8) -> Option<(&'static mut FileEntry, String)> {
    let components = get_path_components(path);
    if components.is_empty() {
        return None;
    }
    
    let filename = components.last().unwrap().clone();
    let parent_components = &components[..components.len() - 1];
    
    unsafe {
        if let Some(ref mut root) = ROOT_FS {
            let mut current = root;
            
            for component in parent_components {
                if let Some(child) = current.children.get_mut(component) {
                    current = child;
                } else {
                    return None;
                }
            }
            
            Some((current, filename))
        } else {
            None
        }
    }
}

pub fn create_file(path: *const u8) -> i32 {
    if let Some((parent, filename)) = find_parent_and_name(path) {
        if parent.children.contains_key(&filename) {
            return -17; // File exists
        }
        
        parent.children.insert(filename.clone(), FileEntry::new_file(filename));
        0
    } else {
        -2 // No such file or directory
    }
}

pub fn create_directory(path: *const u8) -> i32 {
    if let Some((parent, dirname)) = find_parent_and_name(path) {
        if parent.children.contains_key(&dirname) {
            return -17; // Directory exists
        }
        
        parent.children.insert(dirname.clone(), FileEntry::new_directory(dirname));
        0
    } else {
        -2 // No such file or directory
    }
}

pub fn delete_file(path: *const u8) -> i32 {
    if let Some((parent, filename)) = find_parent_and_name(path) {
        if parent.children.remove(&filename).is_some() {
            0
        } else {
            -2 // No such file or directory
        }
    } else {
        -2 // No such file or directory
    }
}

pub fn read_file(path: *const u8, buf: *mut u8, max_len: i32) -> i32 {
    if buf.is_null() || max_len <= 0 {
        return -22; // Invalid argument
    }
    
    if let Some(entry) = find_entry(path) {
        match entry.file_type {
            FileType::Regular => {
                let copy_len = core::cmp::min(entry.data.len(), max_len as usize);
                unsafe {
                    ptr::copy_nonoverlapping(entry.data.as_ptr(), buf, copy_len);
                }
                copy_len as i32
            },
            FileType::Directory => -21, // Is a directory
        }
    } else {
        -2 // No such file or directory
    }
}

pub fn write_file(path: *const u8, buf: *const u8, len: u64) -> u64 {
    if buf.is_null() {
        unsafe { serial_write(b"[VFS-DEBUG] write_file: buf is null\r\n\0".as_ptr()); }
        return 0;
    }
    
    if let Some(entry) = find_entry(path) {
        match entry.file_type {
            FileType::Regular => {
                unsafe { serial_write(b"[VFS-DEBUG] write_file: clearing data\r\n\0".as_ptr()); }
                entry.data.clear();
                unsafe { serial_write(b"[VFS-DEBUG] write_file: reserving data\r\n\0".as_ptr()); }
                entry.data.reserve(len as usize);
                unsafe { serial_write(b"[VFS-DEBUG] write_file: pushing data\r\n\0".as_ptr()); }
                for i in 0..len as usize {
                    entry.data.push(unsafe { *buf.add(i) });
                }
                unsafe { serial_write(b"[VFS-DEBUG] write_file: done\r\n\0".as_ptr()); }
                len
            },
            FileType::Directory => 0, // Can't write to directory
        }
    } else {
        unsafe { serial_write(b"[VFS-DEBUG] write_file: file not found\r\n\0".as_ptr()); }
        0 // File doesn't exist
    }
}

pub fn list_directory(path: *const u8) -> i32 {
    if let Some(entry) = find_entry(path) {
        match entry.file_type {
            FileType::Directory => {
                // Enter critical section to prevent concurrent screen access
                unsafe { sys_cli(); }
                
                for (name, child) in &entry.children {
                    let type_char = match child.file_type {
                        FileType::Directory => b'd',
                        FileType::Regular => b'-',
                    };
                    
                    unsafe {
                        vga_print([type_char, 0].as_ptr());
                        vga_print(b"rwxr-xr-x 1 root root ".as_ptr());
                        
                        // Print file size
                        let size = child.data.len();
                        let mut size_str = [0u8; 16];
                        let mut temp_size = size;
                        let mut i = 0;
                        
                        if temp_size == 0 {
                            size_str[0] = b'0';
                            i = 1;
                        } else {
                            while temp_size > 0 {
                                size_str[i] = b'0' + (temp_size % 10) as u8;
                                temp_size /= 10;
                                i += 1;
                            }
                        }
                        
                        // Reverse digits
                        for j in 0..i/2 {
                            let temp = size_str[j];
                            size_str[j] = size_str[i-1-j];
                            size_str[i-1-j] = temp;
                        }
                        size_str[i] = 0;
                        
                        vga_print(size_str.as_ptr());
                        vga_print(b" Jan  1 00:00 ".as_ptr());
                        
                        // Print filename
                        for &byte in name.as_bytes() {
                            vga_print([byte, 0].as_ptr());
                        }
                        vga_print(b"\n".as_ptr());
                    }
                }
                
                // Exit critical section
                unsafe { sys_sti(); }
                0
            },
            FileType::Regular => -20, // Not a directory
        }
    } else {
        -2 // No such file or directory
    }
}

// C interface functions
#[no_mangle]
pub extern "C" fn rust_vfs_init() {
    init();
}

#[no_mangle]
pub extern "C" fn rust_vfs_create_file(path: *const u8) -> i32 {
    create_file(path)
}

#[no_mangle]
pub extern "C" fn rust_vfs_mkdir(path: *const u8) -> i32 {
    create_directory(path)
}

#[no_mangle]
pub extern "C" fn rust_vfs_unlink(path: *const u8) -> i32 {
    delete_file(path)
}

#[no_mangle]
pub extern "C" fn rust_vfs_read(path: *const u8, buf: *mut u8, max_len: i32) -> i32 {
    read_file(path, buf, max_len)
}

#[no_mangle]
pub extern "C" fn rust_vfs_write(path: *const u8, buf: *const u8, len: u64) -> u64 {
    write_file(path, buf, len)
}

#[no_mangle]
pub extern "C" fn rust_vfs_ls(path: *const u8) -> i32 {
    list_directory(path)
}

#[no_mangle]
pub extern "C" fn rust_vfs_stat(path: *const u8, statbuf: *mut u8) -> i32 {
    if path.is_null() || statbuf.is_null() {
        return -22; // EINVAL
    }
    
    if let Some(entry) = find_entry(path) {
        unsafe {
            // Fill basic stat structure (simplified)
            // struct stat has many fields, we'll fill the basic ones
            let stat_ptr = statbuf as *mut u64;
            
            // st_dev (device ID)
            *stat_ptr.add(0) = 1;
            
            // st_ino (inode number)
            *stat_ptr.add(1) = path as u64; // Use path pointer as pseudo-inode
            
            // st_mode (file type and permissions)
            let mode = match entry.file_type {
                FileType::Directory => 0o040755, // Directory with 755 permissions
                FileType::Regular => 0o100644,   // Regular file with 644 permissions
            };
            *stat_ptr.add(2) = mode;
            
            // st_nlink (number of hard links)
            *stat_ptr.add(3) = 1;
            
            // st_uid, st_gid (user and group IDs)
            *stat_ptr.add(4) = 0; // root
            *stat_ptr.add(5) = 0; // root
            
            // st_size (file size)
            *stat_ptr.add(6) = entry.data.len() as u64;
            
            // st_atime, st_mtime, st_ctime (access, modify, change times)
            let current_time = 1640995200; // Jan 1, 2022 as dummy time
            *stat_ptr.add(7) = current_time;
            *stat_ptr.add(8) = current_time;
            *stat_ptr.add(9) = current_time;
        }
        0
    } else {
        -2 // ENOENT - No such file or directory
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_get_root() -> *mut u8 {
    unsafe {
        if let Some(ref mut root) = ROOT_FS {
            // Return pointer to root filesystem entry
            root as *mut FileEntry as *mut u8
        } else {
            core::ptr::null_mut()
        }
    }
}
