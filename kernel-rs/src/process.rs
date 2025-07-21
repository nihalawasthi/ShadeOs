use core::mem::size_of;
use core::ptr::{null_mut, NonNull};
use alloc::boxed::Box;
use alloc::vec::Vec;
use core::iter::Iterator;
use core::clone::Clone;
use core::option::Option;
use core::option::Option::{Some, None};
use core::convert::AsMut;

extern "C" {
    fn serial_write(s: *const u8);
    fn rust_kmalloc(size: usize) -> *mut u8;
    fn rust_kfree(ptr: *mut u8);
}

// Process states
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ProcessState {
    Ready,
    Running,
    Blocked,
    Zombie,
    Terminated,
}

// Process privilege levels
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PrivilegeLevel {
    Kernel = 0,  // Ring 0
    User = 3,    // Ring 3
}

// Memory region types
#[derive(Debug, Clone, Copy)]
pub enum MemoryRegionType {
    Code,
    Data,
    Stack,
    Heap,
    Shared,
}

// Memory region descriptor
#[repr(C)]
#[derive(Debug, Clone)]
pub struct MemoryRegion {
    pub start_addr: u64,
    pub end_addr: u64,
    pub permissions: u32, // Read=1, Write=2, Execute=4
    pub region_type: MemoryRegionType,
}

// Process Control Block (PCB)
#[repr(C)]
pub struct ProcessControlBlock {
    pub pid: u32,
    pub parent_pid: u32,
    pub state: ProcessState,
    pub privilege_level: PrivilegeLevel,
    
    // CPU context (saved registers)
    pub rax: u64,
    pub rbx: u64,
    pub rcx: u64,
    pub rdx: u64,
    pub rsi: u64,
    pub rdi: u64,
    pub rbp: u64,
    pub rsp: u64,
    pub r8: u64,
    pub r9: u64,
    pub r10: u64,
    pub r11: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    pub rip: u64,
    pub rflags: u64,
    pub cs: u16,
    pub ds: u16,
    pub es: u16,
    pub fs: u16,
    pub gs: u16,
    pub ss: u16,
    
    // Memory management
    pub page_directory: u64, // Physical address of page directory
    pub memory_regions: Vec<MemoryRegion>,
    pub heap_start: u64,
    pub heap_end: u64,
    pub stack_start: u64,
    pub stack_end: u64,
    
    // File descriptors
    pub fd_table: [Option<u32>; 256], // Simple FD table
    pub cwd: [u8; 256], // Current working directory
    
    // Process timing
    pub creation_time: u64,
    pub cpu_time: u64,
    pub priority: i32,
    
    // Exit status
    pub exit_code: i32,
}

impl ProcessControlBlock {
    pub fn new(pid: u32, parent_pid: u32, privilege_level: PrivilegeLevel) -> Self {
        ProcessControlBlock {
            pid,
            parent_pid,
            state: ProcessState::Ready,
            privilege_level,
            
            // Initialize all registers to 0
            rax: 0, rbx: 0, rcx: 0, rdx: 0,
            rsi: 0, rdi: 0, rbp: 0, rsp: 0,
            r8: 0, r9: 0, r10: 0, r11: 0,
            r12: 0, r13: 0, r14: 0, r15: 0,
            rip: 0, rflags: 0x202, // Enable interrupts
            cs: match privilege_level {
                PrivilegeLevel::Kernel => 0x08, // Kernel code segment
                PrivilegeLevel::User => 0x1B,   // User code segment (RPL=3)
            },
            ds: match privilege_level {
                PrivilegeLevel::Kernel => 0x10, // Kernel data segment
                PrivilegeLevel::User => 0x23,   // User data segment (RPL=3)
            },
            es: 0, fs: 0, gs: 0,
            ss: match privilege_level {
                PrivilegeLevel::Kernel => 0x10, // Kernel stack segment
                PrivilegeLevel::User => 0x23,   // User stack segment (RPL=3)
            },
            
            page_directory: 0,
            memory_regions: Vec::new(),
            heap_start: 0,
            heap_end: 0,
            stack_start: 0,
            stack_end: 0,
            
            fd_table: [None; 256],
            cwd: [0; 256],
            
            creation_time: 0,
            cpu_time: 0,
            priority: 0,
            
            exit_code: 0,
        }
    }
    
    pub fn add_memory_region(&mut self, region: MemoryRegion) {
        unsafe { serial_write(b"[PROC-DEBUG] add_memory_region: before push\r\n\0".as_ptr()); }
        self.memory_regions.push(region);
        unsafe { serial_write(b"[PROC-DEBUG] add_memory_region: after push\r\n\0".as_ptr()); }
    }
    
    pub fn check_memory_access(&self, addr: u64, access_type: u32) -> bool {
        for region in &self.memory_regions {
            if addr >= region.start_addr && addr < region.end_addr {
                return (region.permissions & access_type) != 0;
            }
        }
        false
    }
}

// Process manager
pub struct ProcessManager {
    processes: Vec<Box<ProcessControlBlock>>,
    current_pid: u32,
    next_pid: u32,
}

impl ProcessManager {
    pub fn new() -> Self {
        ProcessManager {
            processes: Vec::new(),
            current_pid: 0,
            next_pid: 1,
        }
    }
    
    pub fn create_process(&mut self, parent_pid: u32, privilege_level: PrivilegeLevel) -> u32 {
        let pid = self.next_pid;
        self.next_pid += 1;
        
        let mut pcb = Box::new(ProcessControlBlock::new(pid, parent_pid, privilege_level));
        
        // Set up initial memory regions based on privilege level
        match privilege_level {
            PrivilegeLevel::Kernel => {
                // Kernel process has access to all memory
                pcb.add_memory_region(MemoryRegion {
                    start_addr: 0x0000_0000_0000_0000,
                    end_addr: 0xFFFF_FFFF_FFFF_FFFF,
                    permissions: 7, // Read + Write + Execute
                    region_type: MemoryRegionType::Code,
                });
            },
            PrivilegeLevel::User => {
                // User process gets limited memory regions
                // Code segment (read + execute)
                pcb.add_memory_region(MemoryRegion {
                    start_addr: 0x0000_0000_0040_0000, // 4MB
                    end_addr: 0x0000_0000_0080_0000,   // 8MB
                    permissions: 5, // Read + Execute
                    region_type: MemoryRegionType::Code,
                });
                
                // Data segment (read + write)
                pcb.add_memory_region(MemoryRegion {
                    start_addr: 0x0000_0000_0080_0000, // 8MB
                    end_addr: 0x0000_0000_00C0_0000,   // 12MB
                    permissions: 3, // Read + Write
                    region_type: MemoryRegionType::Data,
                });
                
                // Stack segment (read + write)
                pcb.add_memory_region(MemoryRegion {
                    start_addr: 0x0000_7FFF_FFF0_0000, // High memory
                    end_addr: 0x0000_8000_0000_0000,
                    permissions: 3, // Read + Write
                    region_type: MemoryRegionType::Stack,
                });
                
                pcb.stack_start = 0x0000_7FFF_FFF0_0000;
                pcb.stack_end = 0x0000_8000_0000_0000;
                pcb.rsp = 0x0000_7FFF_FFFF_F000; // Stack pointer near top
            }
        }
        
        // Set current working directory
        pcb.cwd[0] = b'/';
        pcb.cwd[1] = 0;
        
        // Initialize standard file descriptors
        pcb.fd_table[0] = Some(0); // stdin
        pcb.fd_table[1] = Some(1); // stdout
        pcb.fd_table[2] = Some(2); // stderr
        
        self.processes.push(pcb);
        
        unsafe {
            let mut msg = [0u8; 64];
            let prefix = b"[PROCESS] Created process PID=";
            let mut i = 0;
            for &b in prefix { msg[i] = b; i += 1; }
            
            // Convert PID to string
            let mut temp_pid = pid;
            let mut digits = [0u8; 10];
            let mut d = 0;
            if temp_pid == 0 { digits[d] = b'0'; d += 1; }
            while temp_pid > 0 {
                digits[d] = b'0' + (temp_pid % 10) as u8;
                temp_pid /= 10;
                d += 1;
            }
            for j in (0..d).rev() { if i < msg.len() - 3 { msg[i] = digits[j]; i += 1; } }
            
            msg[i] = b'\n'; i += 1;
            msg[i] = 0;
            serial_write(msg.as_ptr());
        }
        
        pid
    }
    
    pub fn get_process(&mut self, pid: u32) -> Option<&mut ProcessControlBlock> {
        self.processes.iter_mut()
            .find(|p| p.pid == pid)
            .map(|p| p.as_mut())
    }
    
    pub fn get_current_process(&mut self) -> Option<&mut ProcessControlBlock> {
        if self.current_pid == 0 { return None; }
        self.get_process(self.current_pid)
    }
    
    pub fn set_current_process(&mut self, pid: u32) {
        self.current_pid = pid;
    }
    
    pub fn terminate_process(&mut self, pid: u32, exit_code: i32) {
        if let Some(process) = self.get_process(pid) {
            process.state = ProcessState::Terminated;
            process.exit_code = exit_code;
            
            unsafe {
                let mut msg = [0u8; 64];
                let prefix = b"[PROCESS] Terminated PID=";
                let mut i = 0;
                for &b in prefix { msg[i] = b; i += 1; }
                
                let mut temp_pid = pid;
                let mut digits = [0u8; 10];
                let mut d = 0;
                if temp_pid == 0 { digits[d] = b'0'; d += 1; }
                while temp_pid > 0 {
                    digits[d] = b'0' + (temp_pid % 10) as u8;
                    temp_pid /= 10;
                    d += 1;
                }
                for j in (0..d).rev() { if i < msg.len() - 3 { msg[i] = digits[j]; i += 1; } }
                
                msg[i] = b'\n'; i += 1;
                msg[i] = 0;
                serial_write(msg.as_ptr());
            }
        }
    }
    
    pub fn list_processes(&self) {
        unsafe {
            serial_write(b"[PROCESS] Process List:\n\0".as_ptr());
            serial_write(b"PID\tPPID\tState\t\tPrivilege\n\0".as_ptr());
        }
        
        for process in &self.processes {
            unsafe {
                let mut msg = [0u8; 128];
                let mut i = 0;
                
                // PID
                let mut temp_pid = process.pid;
                let mut digits = [0u8; 10];
                let mut d = 0;
                if temp_pid == 0 { digits[d] = b'0'; d += 1; }
                while temp_pid > 0 {
                    digits[d] = b'0' + (temp_pid % 10) as u8;
                    temp_pid /= 10;
                    d += 1;
                }
                for j in (0..d).rev() { if i < msg.len() - 3 { msg[i] = digits[j]; i += 1; } }
                msg[i] = b'\t'; i += 1;
                
                // PPID
                let mut temp_ppid = process.parent_pid;
                d = 0;
                if temp_ppid == 0 { digits[d] = b'0'; d += 1; }
                while temp_ppid > 0 {
                    digits[d] = b'0' + (temp_ppid % 10) as u8;
                    temp_ppid /= 10;
                    d += 1;
                }
                for j in (0..d).rev() { if i < msg.len() - 3 { msg[i] = digits[j]; i += 1; } }
                msg[i] = b'\t'; i += 1;
                
                // State
                let state_str = match process.state {
                    ProcessState::Ready => b"Ready     ",
                    ProcessState::Running => b"Running   ",
                    ProcessState::Blocked => b"Blocked   ",
                    ProcessState::Zombie => b"Zombie    ",
                    ProcessState::Terminated => b"Terminated",
                };
                for &b in state_str { if i < msg.len() - 3 { msg[i] = b; i += 1; } }
                
                // Privilege
                let priv_str = match process.privilege_level {
                    PrivilegeLevel::Kernel => b"Kernel ",
                    PrivilegeLevel::User => b"User   ",
                };
                for &b in priv_str { if i < msg.len() - 3 { msg[i] = b; i += 1; } }
                
                msg[i] = 0;
                serial_write(msg.as_ptr());
            }
        }
    }
}

// Global process manager
static mut PROCESS_MANAGER: Option<ProcessManager> = None;

#[no_mangle]
pub extern "C" fn rust_process_init() {
    unsafe {
        PROCESS_MANAGER = Some(ProcessManager::new());
        serial_write(b"[PROCESS] Process manager initialized\n\0".as_ptr());
        
        // Create init process (PID 1)
        if let Some(ref mut pm) = PROCESS_MANAGER {
            let init_pid = pm.create_process(0, PrivilegeLevel::Kernel);
            pm.set_current_process(init_pid);
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_process_create(parent_pid: u32, is_kernel: bool) -> u32 {
    unsafe {
        if let Some(ref mut pm) = PROCESS_MANAGER {
            let privilege = if is_kernel { 
                PrivilegeLevel::Kernel 
            } else { 
                PrivilegeLevel::User 
            };
            pm.create_process(parent_pid, privilege)
        } else {
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_process_terminate(pid: u32, exit_code: i32) {
    unsafe {
        if let Some(ref mut pm) = PROCESS_MANAGER {
            pm.terminate_process(pid, exit_code);
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_process_list() {
    unsafe {
        if let Some(ref pm) = PROCESS_MANAGER {
            pm.list_processes();
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_process_check_access(pid: u32, addr: u64, access_type: u32) -> bool {
    unsafe {
        if let Some(ref mut pm) = PROCESS_MANAGER {
            if let Some(process) = pm.get_process(pid) {
                return process.check_memory_access(addr, access_type);
            }
        }
        false
    }
}

#[no_mangle]
pub extern "C" fn rust_process_get_current_pid() -> u32 {
    unsafe {
        if let Some(ref pm) = PROCESS_MANAGER {
            pm.current_pid
        } else {
            0
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Task {
    pub rsp: u64,
    pub rip: u64,
    pub stack: [u8; 16384],
    pub state: u8,
    pub id: i32,
    pub user_mode: u8,
    pub priority: u8,
    pub cr3: u64,
    pub next: *mut Task,
}
