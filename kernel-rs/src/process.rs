use core::ffi::c_void;

#[repr(C)]
pub struct Task {
    pub rsp: u64,
    pub rip: u64,
    pub stack: [u8; 4096],
    pub state: u32,
    pub id: i32,
    pub user_mode: i32,
    pub priority: i32, // Lower value = higher priority
    pub next: *mut Task,
}

extern "C" {
    pub fn task_create_user(entry: extern "C" fn(), user_stack: *mut c_void, stack_size: i32, arg: *mut c_void) -> i32;
    pub fn map_user_page(virt_addr: u64, phys_addr: u64);
    pub fn alloc_page() -> *mut c_void;
}

/// Create a user process with a mapped user stack.
///
/// # Arguments
/// * `entry` - Entry point for the user process (extern "C" fn)
/// * `stack_size` - Size of the user stack (default: 4096)
/// * `arg` - Argument pointer for the process (optional)
///
/// # Returns
/// * Task ID (int) or -1 on failure
pub fn create_user_process(entry: extern "C" fn(), stack_size: usize, arg: *mut c_void) -> i32 {
    // Allocate a page for the user stack
    let user_stack = unsafe { alloc_page() };
    if user_stack.is_null() { return -1; }
    // Map the stack as user-accessible (assume 1 page for now)
    let user_stack_addr = user_stack as u64;
    unsafe { map_user_page(user_stack_addr, user_stack_addr); }
    // Call the C function to create the user task
    unsafe { task_create_user(entry, user_stack, stack_size as i32, arg) }
} 