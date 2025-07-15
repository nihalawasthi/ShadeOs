use core::ffi::c_void;
use core::ptr;
use crate::process::Task;

extern "C" {
    // C-side task list and helpers
    static mut current: *mut Task;
    fn task_switch(old_rsp: *mut u64, new_rsp: u64);
}

#[no_mangle]
pub extern "C" fn rust_scheduler_tick() {
    unsafe {
        if current.is_null() { return; }
        let prev = current;
        let mut t = current;
        let mut best: *mut Task = core::ptr::null_mut();
        let mut best_priority = i32::MAX;
        let mut first = true;
        // Find the READY task with the highest priority (lowest value)
        loop {
            if (*t).state == 1 /* TASK_READY */ {
                if (*t).priority < best_priority {
                    best_priority = (*t).priority;
                    best = t;
                }
            }
            t = (*t).next;
            if t == current { break; }
        }
        // If multiple tasks have the same priority, pick the next one after current
        if !best.is_null() && best != current {
            let old_rsp = &mut (*prev).rsp as *mut u64;
            let new_rsp = (*best).rsp;
            current = best;
            task_switch(old_rsp, new_rsp);
        }
    }
} 