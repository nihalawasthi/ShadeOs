use crate::vfs::{rust_vfs_ls, rust_vfs_read};
use core::fmt::Write;

const HISTORY_SIZE: usize = 8;
const INPUT_MAX: usize = 128;

pub struct Shell {
    history: [heapless::String<INPUT_MAX>; HISTORY_SIZE],
    hist_pos: usize,
    input: heapless::String<INPUT_MAX>,
}

impl Shell {
    pub fn new() -> Self {
        Self {
            history: Default::default(),
            hist_pos: 0,
            input: heapless::String::new(),
        }
    }

    pub fn run(&mut self) {
        loop {
            self.prompt();
            self.read_input();
            self.handle_command();
        }
    }

    fn prompt(&self) {
        unsafe { crate::vga_print(b"[rust-shell] > \0".as_ptr()); }
    }

    fn read_input(&mut self) {
        self.input.clear();
        // TODO: Implement input from keyboard buffer (Rust side)
        // For now, just simulate a command for demo
        self.input.push_str("help").ok();
    }

    fn handle_command(&mut self) {
        match self.input.as_str() {
            "help" => unsafe { crate::vga_print(b"Commands: help, ls\n\0".as_ptr()); },
            "ls" => unsafe { rust_vfs_ls(b"/\0".as_ptr()); },
            _ => unsafe { crate::vga_print(b"Unknown command\n\0".as_ptr()); },
        }
        // Add to history
        if self.input.len() > 0 {
            self.history[self.hist_pos % HISTORY_SIZE] = self.input.clone();
            self.hist_pos = (self.hist_pos + 1) % HISTORY_SIZE;
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_shell_main() {
    let mut shell = Shell::new();
    shell.run();
} 