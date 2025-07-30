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
        
        // Simple keyboard input simulation - in a real implementation
        // this would read from a keyboard buffer or interrupt handler
        unsafe {
            extern "C" {
                fn keyboard_get_char() -> u8;
                fn keyboard_has_input() -> i32;
            }
            
            // Check if there's keyboard input available
            if keyboard_has_input() != 0 {
                let mut ch = keyboard_get_char();
                while ch != 0 && ch != b'\n' && ch != b'\r' && self.input.len() < INPUT_MAX - 1 {
                    if ch == 8 || ch == 127 { // Backspace or DEL
                        if !self.input.is_empty() {
                            self.input.pop();
                        }
                    } else if ch >= 32 && ch <= 126 { // Printable ASCII
                        self.input.push(ch as char).ok();
                    }
                    
                    // Check for more input
                    if keyboard_has_input() != 0 {
                        ch = keyboard_get_char();
                    } else {
                        break;
                    }
                }
            } else {
                // For demo purposes, provide some default commands to cycle through
                static mut DEMO_COUNTER: usize = 0;
                let demo_commands = ["help", "ls", "ps", "clear"];
                self.input.push_str(demo_commands[DEMO_COUNTER % demo_commands.len()]).ok();
                DEMO_COUNTER += 1;
            }
        }
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