extern "C" {
    fn serial_write(s: *const u8);
    fn vga_print(s: *const u8);
    fn rust_kmalloc(size: usize) -> *mut u8;
    fn rust_kfree(ptr: *mut u8);
    fn memcpy(dest: *mut u8, src: *const u8, n: usize);
    fn memset(s: *mut u8, c: i32, n: usize);
    fn strcmp(s1: *const u8, s2: *const u8) -> i32;
    fn strrchr(s: *const u8, c: i32) -> *const u8;
    
    // ext2 C functions
    fn ext2_c_init(device: *const u8) -> i32;
    fn ext2_vfs_mount(path: *const u8, device: *const u8) -> i32;
    fn ext2_vfs_open(path: *const u8, flags: i32) -> i32;
    fn ext2_vfs_read(path: *const u8, buf: *mut u8, max_len: i32) -> i32;
    fn ext2_vfs_write(path: *const u8, buf: *const u8, len: i32) -> i32;
    fn ext2_vfs_mkdir(path: *const u8) -> i32;
    fn ext2_vfs_rmdir(path: *const u8) -> i32;
    fn ext2_vfs_unlink(path: *const u8) -> i32;
    fn ext2_vfs_stat(path: *const u8, statbuf: *mut u8) -> i32;
    
    // Block device functions
    fn blockdev_get(id: i32) -> *const u8;
}

// ext2 filesystem context
pub struct Ext2Fs {
    pub mounted: bool,
    pub device_id: i32,
}

impl Ext2Fs {
    pub fn new() -> Self {
        Ext2Fs {
            mounted: false,
            device_id: 0,
        }
    }
    
    pub fn mount(&mut self, device_id: i32) -> i32 {
        unsafe {
            serial_write(b"[EXT2-RS] Mounting ext2 filesystem on device \0".as_ptr());
            serial_write_hex(device_id as u64);
            serial_write(b"\n\0".as_ptr());
            
            let device = blockdev_get(device_id);
            if device.is_null() {
                serial_write(b"[EXT2-RS] Invalid device\n\0".as_ptr());
                return -1;
            }
            
            let result = ext2_c_init(device);
            if result == 0 {
                self.mounted = true;
                self.device_id = device_id;
                serial_write(b"[EXT2-RS] ext2 filesystem mounted successfully\n\0".as_ptr());
            } else {
                serial_write(b"[EXT2-RS] Failed to mount ext2 filesystem\n\0".as_ptr());
            }
            
            result
        }
    }
    
    pub fn unmount(&mut self) -> i32 {
        if !self.mounted {
            return -1;
        }
        
        unsafe {
            serial_write(b"[EXT2-RS] Unmounting ext2 filesystem\n\0".as_ptr());
            // In a real implementation, we'd call ext2_umount here
            self.mounted = false;
            self.device_id = 0;
        }
        
        0
    }
    
    pub fn open(&self, path: &str) -> i32 {
        if !self.mounted {
            return -1;
        }
        
        unsafe {
            let path_ptr = path.as_ptr();
            ext2_vfs_open(path_ptr, 0) // O_RDONLY
        }
    }
    
    pub fn read(&self, path: &str, buf: &mut [u8]) -> i32 {
        if !self.mounted {
            return -1;
        }
        
        unsafe {
            let path_ptr = path.as_ptr();
            let buf_ptr = buf.as_mut_ptr();
            let max_len = buf.len() as i32;
            
            ext2_vfs_read(path_ptr, buf_ptr, max_len)
        }
    }
    
    pub fn write(&self, path: &str, data: &[u8]) -> i32 {
        if !self.mounted {
            return -1;
        }
        
        unsafe {
            let path_ptr = path.as_ptr();
            let data_ptr = data.as_ptr();
            let len = data.len() as i32;
            
            ext2_vfs_write(path_ptr, data_ptr, len)
        }
    }
    
    pub fn mkdir(&self, path: &str) -> i32 {
        if !self.mounted {
            return -1;
        }
        
        unsafe {
            let path_ptr = path.as_ptr();
            ext2_vfs_mkdir(path_ptr)
        }
    }
    
    pub fn rmdir(&self, path: &str) -> i32 {
        if !self.mounted {
            return -1;
        }
        
        unsafe {
            let path_ptr = path.as_ptr();
            ext2_vfs_rmdir(path_ptr)
        }
    }
    
    pub fn unlink(&self, path: &str) -> i32 {
        if !self.mounted {
            return -1;
        }
        
        unsafe {
            let path_ptr = path.as_ptr();
            ext2_vfs_unlink(path_ptr)
        }
    }
    
    pub fn stat(&self, path: &str) -> Option<FileStat> {
        if !self.mounted {
            return None;
        }
        
        unsafe {
            let path_ptr = path.as_ptr();
            let stat_buf = rust_kmalloc(80) as *mut u64; // Simplified stat structure
            
            if ext2_vfs_stat(path_ptr, stat_buf as *mut u8) == 0 {
                let stat = FileStat {
                    st_dev: *stat_buf.add(0),
                    st_ino: *stat_buf.add(1),
                    st_mode: *stat_buf.add(2) as u16,
                    st_nlink: *stat_buf.add(3) as u16,
                    st_uid: *stat_buf.add(4) as u16,
                    st_gid: *stat_buf.add(5) as u16,
                    st_size: *stat_buf.add(6),
                    st_atime: *stat_buf.add(7),
                    st_mtime: *stat_buf.add(8),
                    st_ctime: *stat_buf.add(9),
                };
                
                            rust_kfree(stat_buf as *mut u8);
            Some(stat)
            } else {
                            rust_kfree(stat_buf as *mut u8);
            None
            }
        }
    }
}

// File statistics structure
#[derive(Debug, Clone)]
pub struct FileStat {
    pub st_dev: u64,
    pub st_ino: u64,
    pub st_mode: u16,
    pub st_nlink: u16,
    pub st_uid: u16,
    pub st_gid: u16,
    pub st_size: u64,
    pub st_atime: u64,
    pub st_mtime: u64,
    pub st_ctime: u64,
}

impl FileStat {
    pub fn is_file(&self) -> bool {
        (self.st_mode & 0xF000) == 0x8000
    }
    
    pub fn is_directory(&self) -> bool {
        (self.st_mode & 0xF000) == 0x4000
    }
    
    pub fn is_symlink(&self) -> bool {
        (self.st_mode & 0xF000) == 0xA000
    }
    
    pub fn permissions(&self) -> u16 {
        self.st_mode & 0x0FFF
    }
}

// Global ext2 filesystem instance
static mut EXT2_FS: Ext2Fs = Ext2Fs {
    mounted: false,
    device_id: 0,
};

// Public interface functions
pub fn ext2_init() {
    unsafe {
        serial_write(b"[EXT2-RS] Initializing ext2 filesystem support\n\0".as_ptr());
        EXT2_FS = Ext2Fs::new();
        serial_write(b"[EXT2-RS] ext2 filesystem support initialized\n\0".as_ptr());
    }
}

pub fn ext2_mount(device_id: i32) -> i32 {
    unsafe {
        EXT2_FS.mount(device_id)
    }
}

pub fn ext2_unmount() -> i32 {
    unsafe {
        EXT2_FS.unmount()
    }
}

pub fn ext2_open(path: &str) -> i32 {
    unsafe {
        EXT2_FS.open(path)
    }
}

pub fn ext2_read(path: &str, buf: &mut [u8]) -> i32 {
    unsafe {
        EXT2_FS.read(path, buf)
    }
}

pub fn ext2_write(path: &str, data: &[u8]) -> i32 {
    unsafe {
        EXT2_FS.write(path, data)
    }
}

pub fn ext2_mkdir(path: &str) -> i32 {
    unsafe {
        EXT2_FS.mkdir(path)
    }
}

pub fn ext2_rmdir(path: &str) -> i32 {
    unsafe {
        EXT2_FS.rmdir(path)
    }
}

pub fn ext2_unlink(path: &str) -> i32 {
    unsafe {
        EXT2_FS.unlink(path)
    }
}

pub fn ext2_stat(path: &str) -> Option<FileStat> {
    unsafe {
        EXT2_FS.stat(path)
    }
}

// Test function to demonstrate ext2 functionality
pub fn ext2_test() {
    unsafe {
        serial_write(b"[EXT2-RS] Testing ext2 filesystem functionality\n\0".as_ptr());
        
        // Try to mount on device 0
        let mount_result = ext2_mount(0);
        serial_write(b"[EXT2-RS] Mount result: \0".as_ptr());
        serial_write_hex(mount_result as u64);
        serial_write(b"\n\0".as_ptr());
        
        if mount_result == 0 {
            // Test file operations
            let test_path = "/test.txt";
            let test_data = b"Hello from ext2 filesystem!";
            
            // Try to write a file
            let write_result = ext2_write(test_path, test_data);
            serial_write(b"[EXT2-RS] Write result: \0".as_ptr());
            serial_write_hex(write_result as u64);
            serial_write(b"\n\0".as_ptr());
            
            // Try to read the file
            let mut read_buf = [0u8; 256];
            let read_result = ext2_read(test_path, &mut read_buf);
            serial_write(b"[EXT2-RS] Read result: \0".as_ptr());
            serial_write_hex(read_result as u64);
            serial_write(b"\n\0".as_ptr());
            
            if read_result > 0 {
                serial_write(b"[EXT2-RS] Read data: \0".as_ptr());
                for i in 0..read_result as usize {
                    let byte = [read_buf[i], 0];
                    serial_write(byte.as_ptr());
                }
                serial_write(b"\n\0".as_ptr());
            }
            
            // Test directory creation
            let mkdir_result = ext2_mkdir("/testdir");
            serial_write(b"[EXT2-RS] Mkdir result: \0".as_ptr());
            serial_write_hex(mkdir_result as u64);
            serial_write(b"\n\0".as_ptr());
            
            // Test stat
            if let Some(stat) = ext2_stat(test_path) {
                serial_write(b"[EXT2-RS] File size: \0".as_ptr());
                serial_write_hex(stat.st_size);
                serial_write(b"\n\0".as_ptr());
            }
            
            // Unmount
            ext2_unmount();
        }
        
        serial_write(b"[EXT2-RS] ext2 filesystem test completed\n\0".as_ptr());
    }
}

// Helper function to write hex values
fn serial_write_hex(value: u64) {
    unsafe {
        let mut buf = [b'0', b'x', b'0', b'0', b'0', b'0', b'0', b'0', b'0', b'0', 0];
        let mut val = value;
        
        for i in (2..10).rev() {
            let digit = (val & 0xF) as u8;
            buf[i] = if digit < 10 { b'0' + digit } else { b'a' + (digit - 10) };
            val >>= 4;
        }
        
        serial_write(buf.as_ptr());
    }
}
