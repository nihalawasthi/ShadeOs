use crate::process::{ProcessManager, PrivilegeLevel};
use crate::vfs;
use alloc::vec::Vec;

#[no_mangle]
pub extern "C" fn rust_syscall_init() {
    // Initialize system calls
    unsafe {
        serial_write(b"[SYSCALL] Initializing system calls\n\0".as_ptr());
    }
    // Additional logic would be added here if needed for syscalls
    // Add any initialization specific to syscall setup
}

extern "C" {
    fn serial_write(s: *const u8);
    fn rust_process_get_current_pid() -> u32;
    fn rust_process_check_access(pid: u32, addr: u64, access_type: u32) -> bool;
    fn rust_vfs_read(path_ptr: *const u8, buf_ptr: *mut u8, max_len: i32) -> i32;
    fn rust_vfs_write(path_ptr: *const u8, buf_ptr: *const u8, len: u64) -> u64;
    fn rust_vfs_create_file(path_ptr: *const u8) -> i32;
    fn rust_vfs_mkdir(path_ptr: *const u8) -> i32;
    fn rust_vfs_unlink(path_ptr: *const u8) -> i32;
    fn rust_vfs_ls(path_ptr: *const u8) -> i32;
}

// System call numbers
pub const SYS_READ: u64 = 0;
pub const SYS_WRITE: u64 = 1;
pub const SYS_OPEN: u64 = 2;
pub const SYS_CLOSE: u64 = 3;
pub const SYS_STAT: u64 = 4;
pub const SYS_FSTAT: u64 = 5;
pub const SYS_LSTAT: u64 = 6;
pub const SYS_POLL: u64 = 7;
pub const SYS_LSEEK: u64 = 8;
pub const SYS_MMAP: u64 = 9;
pub const SYS_MPROTECT: u64 = 10;
pub const SYS_MUNMAP: u64 = 11;
pub const SYS_BRK: u64 = 12;
pub const SYS_RT_SIGACTION: u64 = 13;
pub const SYS_RT_SIGPROCMASK: u64 = 14;
pub const SYS_RT_SIGRETURN: u64 = 15;
pub const SYS_IOCTL: u64 = 16;
pub const SYS_PREAD64: u64 = 17;
pub const SYS_PWRITE64: u64 = 18;
pub const SYS_READV: u64 = 19;
pub const SYS_WRITEV: u64 = 20;
pub const SYS_ACCESS: u64 = 21;
pub const SYS_PIPE: u64 = 22;
pub const SYS_SELECT: u64 = 23;
pub const SYS_SCHED_YIELD: u64 = 24;
pub const SYS_MREMAP: u64 = 25;
pub const SYS_MSYNC: u64 = 26;
pub const SYS_MINCORE: u64 = 27;
pub const SYS_MADVISE: u64 = 28;
pub const SYS_SHMGET: u64 = 29;
pub const SYS_SHMAT: u64 = 30;
pub const SYS_SHMCTL: u64 = 31;
pub const SYS_DUP: u64 = 32;
pub const SYS_DUP2: u64 = 33;
pub const SYS_PAUSE: u64 = 34;
pub const SYS_NANOSLEEP: u64 = 35;
pub const SYS_GETITIMER: u64 = 36;
pub const SYS_ALARM: u64 = 37;
pub const SYS_SETITIMER: u64 = 38;
pub const SYS_GETPID: u64 = 39;
pub const SYS_SENDFILE: u64 = 40;
pub const SYS_SOCKET: u64 = 41;
pub const SYS_CONNECT: u64 = 42;
pub const SYS_ACCEPT: u64 = 43;
pub const SYS_SENDTO: u64 = 44;
pub const SYS_RECVFROM: u64 = 45;
pub const SYS_SENDMSG: u64 = 46;
pub const SYS_RECVMSG: u64 = 47;
pub const SYS_SHUTDOWN: u64 = 48;
pub const SYS_BIND: u64 = 49;
pub const SYS_LISTEN: u64 = 50;
pub const SYS_GETSOCKNAME: u64 = 51;
pub const SYS_GETPEERNAME: u64 = 52;
pub const SYS_SOCKETPAIR: u64 = 53;
pub const SYS_SETSOCKOPT: u64 = 54;
pub const SYS_GETSOCKOPT: u64 = 55;
pub const SYS_CLONE: u64 = 56;
pub const SYS_FORK: u64 = 57;
pub const SYS_VFORK: u64 = 58;
pub const SYS_EXECVE: u64 = 59;
pub const SYS_EXIT: u64 = 60;
pub const SYS_WAIT4: u64 = 61;
pub const SYS_KILL: u64 = 62;
pub const SYS_UNAME: u64 = 63;
pub const SYS_SEMGET: u64 = 64;
pub const SYS_SEMOP: u64 = 65;
pub const SYS_SEMCTL: u64 = 66;
pub const SYS_SHMDT: u64 = 67;
pub const SYS_MSGGET: u64 = 68;
pub const SYS_MSGSND: u64 = 69;
pub const SYS_MSGRCV: u64 = 70;
pub const SYS_MSGCTL: u64 = 71;
pub const SYS_FCNTL: u64 = 72;
pub const SYS_FLOCK: u64 = 73;
pub const SYS_FSYNC: u64 = 74;
pub const SYS_FDATASYNC: u64 = 75;
pub const SYS_TRUNCATE: u64 = 76;
pub const SYS_FTRUNCATE: u64 = 77;
pub const SYS_GETDENTS: u64 = 78;
pub const SYS_GETCWD: u64 = 79;
pub const SYS_CHDIR: u64 = 80;
pub const SYS_FCHDIR: u64 = 81;
pub const SYS_RENAME: u64 = 82;
pub const SYS_MKDIR: u64 = 83;
pub const SYS_RMDIR: u64 = 84;
pub const SYS_CREAT: u64 = 85;
pub const SYS_LINK: u64 = 86;
pub const SYS_UNLINK: u64 = 87;
pub const SYS_SYMLINK: u64 = 88;
pub const SYS_READLINK: u64 = 89;
pub const SYS_CHMOD: u64 = 90;
pub const SYS_FCHMOD: u64 = 91;
pub const SYS_CHOWN: u64 = 92;
pub const SYS_FCHOWN: u64 = 93;
pub const SYS_LCHOWN: u64 = 94;
pub const SYS_UMASK: u64 = 95;
pub const SYS_GETTIMEOFDAY: u64 = 96;
pub const SYS_GETRLIMIT: u64 = 97;
pub const SYS_GETRUSAGE: u64 = 98;
pub const SYS_SYSINFO: u64 = 99;

// Error codes
pub const EPERM: i64 = -1;      // Operation not permitted
pub const ENOENT: i64 = -2;     // No such file or directory
pub const ESRCH: i64 = -3;      // No such process
pub const EINTR: i64 = -4;      // Interrupted system call
pub const EIO: i64 = -5;        // I/O error
pub const ENXIO: i64 = -6;      // No such device or address
pub const E2BIG: i64 = -7;      // Argument list too long
pub const ENOEXEC: i64 = -8;    // Exec format error
pub const EBADF: i64 = -9;      // Bad file number
pub const ECHILD: i64 = -10;    // No child processes
pub const EAGAIN: i64 = -11;    // Try again
pub const ENOMEM: i64 = -12;    // Out of memory
pub const EACCES: i64 = -13;    // Permission denied
pub const EFAULT: i64 = -14;    // Bad address
pub const ENOTBLK: i64 = -15;   // Block device required
pub const EBUSY: i64 = -16;     // Device or resource busy
pub const EEXIST: i64 = -17;    // File exists
pub const EXDEV: i64 = -18;     // Cross-device link
pub const ENODEV: i64 = -19;    // No such device
pub const ENOTDIR: i64 = -20;   // Not a directory
pub const EISDIR: i64 = -21;    // Is a directory
pub const EINVAL: i64 = -22;    // Invalid argument
pub const ENFILE: i64 = -23;    // File table overflow
pub const EMFILE: i64 = -24;    // Too many open files
pub const ENOTTY: i64 = -25;    // Not a typewriter
pub const ETXTBSY: i64 = -26;   // Text file busy
pub const EFBIG: i64 = -27;     // File too large
pub const ENOSPC: i64 = -28;    // No space left on device
pub const ESPIPE: i64 = -29;    // Illegal seek
pub const EROFS: i64 = -30;     // Read-only file system
pub const EMLINK: i64 = -31;    // Too many links
pub const EPIPE: i64 = -32;     // Broken pipe
pub const EDOM: i64 = -33;      // Math argument out of domain of func
pub const ERANGE: i64 = -34;    // Math result not representable

// System call handler
#[no_mangle]
pub extern "C" fn rust_syscall_handler(
    syscall_num: u64,
    arg1: u64,
    arg2: u64,
    arg3: u64,
    arg4: u64,
    arg5: u64,
    arg6: u64,
) -> i64 {
    // Get current process ID for privilege checking
    let current_pid = unsafe { rust_process_get_current_pid() };
    
    // Log system call for debugging
    unsafe {
        let mut msg = [0u8; 128];
        let mut i = 0;
        let prefix = b"[SYSCALL] PID=";
        for &b in prefix { msg[i] = b; i += 1; }
        
        // PID
        let mut temp_pid = current_pid;
        let mut digits = [0u8; 10];
        let mut d = 0;
        if temp_pid == 0 { digits[d] = b'0'; d += 1; }
        while temp_pid > 0 {
            digits[d] = b'0' + (temp_pid % 10) as u8;
            temp_pid /= 10;
            d += 1;
        }
        for j in (0..d).rev() { if i < msg.len() - 3 { msg[i] = digits[j]; i += 1; } }
        
        let middle = b" syscall=";
        for &b in middle { msg[i] = b; i += 1; }
        
        // Syscall number
        let mut temp_sys = syscall_num;
        d = 0;
        if temp_sys == 0 { digits[d] = b'0'; d += 1; }
        while temp_sys > 0 {
            digits[d] = b'0' + (temp_sys % 10) as u8;
            temp_sys /= 10;
            d += 1;
        }
        for j in (0..d).rev() { if i < msg.len() - 3 { msg[i] = digits[j]; i += 1; } }
        
        msg[i] = b'\n'; i += 1;
        msg[i] = 0;
        serial_write(msg.as_ptr());
    }
    
    match syscall_num {
        SYS_READ => sys_read(arg1 as i32, arg2 as *mut u8, arg3 as usize),
        SYS_WRITE => sys_write(arg1 as i32, arg2 as *const u8, arg3 as usize),
        SYS_OPEN => sys_open(arg1 as *const u8, arg2 as i32, arg3 as u32),
        SYS_CLOSE => sys_close(arg1 as i32),
        SYS_GETPID => sys_getpid(),
        SYS_EXIT => sys_exit(arg1 as i32),
        SYS_FORK => sys_fork(),
        SYS_EXECVE => sys_execve(arg1 as *const u8, arg2 as *const *const u8, arg3 as *const *const u8),
        SYS_WAIT4 => sys_wait4(arg1 as i32, arg2 as *mut i32, arg3 as i32, arg4 as *mut u8),
        SYS_KILL => sys_kill(arg1 as i32, arg2 as i32),
        SYS_CHDIR => sys_chdir(arg1 as *const u8),
        SYS_GETCWD => sys_getcwd(arg1 as *mut u8, arg2 as usize),
        SYS_MKDIR => sys_mkdir(arg1 as *const u8, arg2 as u32),
        SYS_RMDIR => sys_rmdir(arg1 as *const u8),
        SYS_UNLINK => sys_unlink(arg1 as *const u8),
        SYS_CREAT => sys_creat(arg1 as *const u8, arg2 as u32),
        SYS_ACCESS => sys_access(arg1 as *const u8, arg2 as i32),
        SYS_BRK => sys_brk(arg1 as *mut u8),
        SYS_MMAP => sys_mmap(arg1 as *mut u8, arg2 as usize, arg3 as i32, arg4 as i32, arg5 as i32, arg6 as i64),
        SYS_MUNMAP => sys_munmap(arg1 as *mut u8, arg2 as usize),
        SYS_UNAME => sys_uname(arg1 as *mut u8),
        SYS_GETTIMEOFDAY => sys_gettimeofday(arg1 as *mut u8, arg2 as *mut u8),
        SYS_SCHED_YIELD => sys_sched_yield(),
        _ => {
            unsafe {
                serial_write(b"[SYSCALL] Unimplemented system call\n\0".as_ptr());
            }
            EINVAL
        }
    }
}

// System call implementations
fn sys_read(fd: i32, buf: *mut u8, count: usize) -> i64 {
    if buf.is_null() || count == 0 {
        return EINVAL;
    }
    
    // Check memory access permissions
    let current_pid = unsafe { rust_process_get_current_pid() };
    if !unsafe { rust_process_check_access(current_pid, buf as u64, 2) } { // Write access
        return EFAULT;
    }
    
    match fd {
        0 => { // stdin - for now, return 0 (EOF)
            0
        },
        1 | 2 => EBADF, // stdout/stderr are not readable
        _ => {
            // File descriptor - implement file reading
            // For now, return error
            EBADF
        }
    }
}

fn sys_write(fd: i32, buf: *const u8, count: usize) -> i64 {
    if buf.is_null() || count == 0 {
        return EINVAL;
    }
    
    // Check memory access permissions
    let current_pid = unsafe { rust_process_get_current_pid() };
    if !unsafe { rust_process_check_access(current_pid, buf as u64, 1) } { // Read access
        return EFAULT;
    }
    
    match fd {
        1 | 2 => { // stdout/stderr
            unsafe {
                // Write to VGA/serial
                for i in 0..count {
                    let byte = *buf.add(i);
                    if byte == 0 { break; }
                    // Simple character output - in real implementation would use VGA driver
                    let char_buf = [byte, 0];
                    // For now, just write to serial for debugging
                    // In full implementation, this would write to console
                }
                serial_write(b"[SYSCALL] sys_write to stdout/stderr\n\0".as_ptr());
            }
            count as i64
        },
        0 => EBADF, // stdin is not writable
        _ => {
            // File descriptor - implement file writing
            EBADF
        }
    }
}

fn sys_open(pathname: *const u8, flags: i32, mode: u32) -> i64 {
    if pathname.is_null() {
        return EINVAL;
    }
    
    // Check memory access permissions
    let current_pid = unsafe { rust_process_get_current_pid() };
    if !unsafe { rust_process_check_access(current_pid, pathname as u64, 1) } {
        return EFAULT;
    }
    
    // For now, return a dummy file descriptor
    unsafe {
        serial_write(b"[SYSCALL] sys_open - not fully implemented\n\0".as_ptr());
    }
    3 // Return fd 3 as a placeholder
}

fn sys_close(fd: i32) -> i64 {
    match fd {
        0..=2 => EBADF, // Can't close stdin/stdout/stderr
        _ => {
            unsafe {
                serial_write(b"[SYSCALL] sys_close - not fully implemented\n\0".as_ptr());
            }
            0 // Success
        }
    }
}

fn sys_getpid() -> i64 {
    unsafe { rust_process_get_current_pid() as i64 }
}

fn sys_exit(status: i32) -> i64 {
    let current_pid = unsafe { rust_process_get_current_pid() };
    unsafe {
        extern "C" {
            fn rust_process_terminate(pid: u32, exit_code: i32);
        }
        rust_process_terminate(current_pid, status);
        serial_write(b"[SYSCALL] Process exited\n\0".as_ptr());
    }
    // This should not return, but if it does:
    0
}

fn sys_fork() -> i64 {
    unsafe {
        extern "C" {
            fn rust_process_create(parent_pid: u32, is_kernel: bool) -> u32;
        }
        let current_pid = rust_process_get_current_pid();
        let new_pid = rust_process_create(current_pid, false); // Create user process
        
        if new_pid == 0 {
            ENOMEM // Failed to create process
        } else {
            new_pid as i64 // Return child PID to parent
        }
    }
}

fn sys_execve(filename: *const u8, argv: *const *const u8, envp: *const *const u8) -> i64 {
    if filename.is_null() {
        return EINVAL;
    }
    
    unsafe {
        serial_write(b"[SYSCALL] sys_execve - not fully implemented\n\0".as_ptr());
    }
    ENOEXEC // Exec format error
}

fn sys_wait4(pid: i32, status: *mut i32, options: i32, rusage: *mut u8) -> i64 {
    unsafe {
        serial_write(b"[SYSCALL] sys_wait4 - not fully implemented\n\0".as_ptr());
    }
    ECHILD // No child processes
}

fn sys_kill(pid: i32, sig: i32) -> i64 {
    if pid <= 0 {
        return EINVAL;
    }
    
    unsafe {
        extern "C" {
            fn rust_process_terminate(pid: u32, exit_code: i32);
        }
        rust_process_terminate(pid as u32, sig);
        serial_write(b"[SYSCALL] sys_kill - process terminated\n\0".as_ptr());
    }
    0
}

fn sys_chdir(path: *const u8) -> i64 {
    if path.is_null() {
        return EINVAL;
    }
    
    unsafe {
        let result = rust_vfs_ls(path); // Check if directory exists
        if result == 0 {
            serial_write(b"[SYSCALL] sys_chdir - success\n\0".as_ptr());
            0
        } else {
            ENOENT
        }
    }
}

fn sys_getcwd(buf: *mut u8, size: usize) -> i64 {
    if buf.is_null() || size == 0 {
        return EINVAL;
    }
    
    // For now, always return root directory
    unsafe {
        *buf = b'/';
        *buf.add(1) = 0;
    }
    buf as i64
}

fn sys_mkdir(pathname: *const u8, mode: u32) -> i64 {
    if pathname.is_null() {
        return EINVAL;
    }
    
    unsafe {
        let result = rust_vfs_mkdir(pathname);
        if result == 0 {
            0
        } else {
            EEXIST
        }
    }
}

fn sys_rmdir(pathname: *const u8) -> i64 {
    if pathname.is_null() {
        return EINVAL;
    }
    
    unsafe {
        let result = rust_vfs_unlink(pathname);
        if result == 0 {
            0
        } else {
            ENOENT
        }
    }
}

fn sys_unlink(pathname: *const u8) -> i64 {
    if pathname.is_null() {
        return EINVAL;
    }
    
    unsafe {
        let result = rust_vfs_unlink(pathname);
        if result == 0 {
            0
        } else {
            ENOENT
        }
    }
}

fn sys_creat(pathname: *const u8, mode: u32) -> i64 {
    if pathname.is_null() {
        return EINVAL;
    }
    
    unsafe {
        let result = rust_vfs_create_file(pathname);
        if result == 0 || result == -17 { // Success or already exists
            3 // Return dummy fd
        } else {
            EACCES
        }
    }
}

fn sys_access(pathname: *const u8, mode: i32) -> i64 {
    if pathname.is_null() {
        return EINVAL;
    }
    
    // For now, always return success
    0
}

fn sys_brk(addr: *mut u8) -> i64 {
    // Simple heap management - not fully implemented
    unsafe {
        serial_write(b"[SYSCALL] sys_brk - not fully implemented\n\0".as_ptr());
    }
    addr as i64
}

fn sys_mmap(addr: *mut u8, length: usize, prot: i32, flags: i32, fd: i32, offset: i64) -> i64 {
    unsafe {
        serial_write(b"[SYSCALL] sys_mmap - not fully implemented\n\0".as_ptr());
    }
    ENOMEM
}

fn sys_munmap(addr: *mut u8, length: usize) -> i64 {
    unsafe {
        serial_write(b"[SYSCALL] sys_munmap - not fully implemented\n\0".as_ptr());
    }
    0
}

fn sys_uname(buf: *mut u8) -> i64 {
    if buf.is_null() {
        return EINVAL;
    }
    
    // Fill uname structure
    unsafe {
        let uname_info = b"ShadeOS\0\0\0\0\0\0\0\0\0shadeos\0\0\0\0\0\0\0\0\01.0.0\0\0\0\0\0\0\0\0\0\0\0\0#1 SMP\0\0\0\0\0\0\0\0\0\0x86_64\0\0\0\0\0\0\0\0\0\0";
        core::ptr::copy_nonoverlapping(uname_info.as_ptr(), buf, uname_info.len());
    }
    0
}

fn sys_gettimeofday(tv: *mut u8, tz: *mut u8) -> i64 {
    if tv.is_null() {
        return EINVAL;
    }
    
    // Return dummy time values
    unsafe {
        // struct timeval { tv_sec: i64, tv_usec: i64 }
        *(tv as *mut i64) = 1640995200; // Jan 1, 2022
        *(tv.add(8) as *mut i64) = 0;   // microseconds
    }
    0
}

fn sys_sched_yield() -> i64 {
    unsafe {
        serial_write(b"[SYSCALL] sys_sched_yield - yielding CPU\n\0".as_ptr());
    }
    0
}
