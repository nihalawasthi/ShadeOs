use alloc::collections::VecDeque;
use core::sync::atomic::{AtomicBool, Ordering};
use core::option::Option;
use core::option::Option::{Some, None};

extern "C" {
    fn serial_write(s: *const u8);
    fn inb(port: u16) -> u8;
    fn outb(port: u16, value: u8);
}

const KEYBOARD_DATA_PORT: u16 = 0x60;
const KEYBOARD_STATUS_PORT: u16 = 0x64;

static mut KEYBOARD_BUFFER: Option<VecDeque<u8>> = None;
static KEYBOARD_INITIALIZED: AtomicBool = AtomicBool::new(false);

// US QWERTY scancode to ASCII mapping
static SCANCODE_TO_ASCII: [u8; 128] = [
    0, 27, b'1', b'2', b'3', b'4', b'5', b'6', b'7', b'8', b'9', b'0', b'-', b'=', 8, // Backspace
    b'\t', b'q', b'w', b'e', b'r', b't', b'y', b'u', b'i', b'o', b'p', b'[', b']', b'\n', // Enter
    0, // Ctrl
    b'a', b's', b'd', b'f', b'g', b'h', b'j', b'k', b'l', b';', b'\'', b'`',
    0, // Left shift
    b'\\', b'z', b'x', b'c', b'v', b'b', b'n', b'm', b',', b'.', b'/', 
    0, // Right shift
    b'*',
    0, // Alt
    b' ', // Space
    0, // Caps lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1-F10
    0, // Num lock
    0, // Scroll lock
    0, // Home
    0, // Up arrow
    0, // Page up
    b'-',
    0, // Left arrow
    0,
    0, // Right arrow
    b'+',
    0, // End
    0, // Down arrow
    0, // Page down
    0, // Insert
    0, // Delete
    // Padding to 128 elements
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
];

pub fn init() {
    rust_keyboard_init();
}

#[no_mangle]
pub extern "C" fn rust_keyboard_init() {
    unsafe {
        serial_write(b"[RUST KB] Keyboard buffer initialized\n\0".as_ptr());
        KEYBOARD_BUFFER = Some(VecDeque::new());
        KEYBOARD_INITIALIZED.store(true, Ordering::SeqCst);
    }
}

pub fn handle_keyboard_interrupt() {
    unsafe {
        let scancode = inb(KEYBOARD_DATA_PORT);
        
        // Only handle key press events (bit 7 clear)
        if scancode & 0x80 == 0 {
            if let Some(ascii) = scancode_to_ascii(scancode) {
                if let Some(ref mut buffer) = KEYBOARD_BUFFER {
                    if buffer.len() < 256 { // Prevent buffer overflow
                        buffer.push_back(ascii);
                    }
                }
            }
        }
    }
}

fn scancode_to_ascii(scancode: u8) -> Option<u8> {
    if (scancode as usize) < SCANCODE_TO_ASCII.len() {
        let ascii = SCANCODE_TO_ASCII[scancode as usize];
        if ascii != 0 {
            Some(ascii)
        } else {
            None
        }
    } else {
        None
    }
}

pub fn get_char() -> i32 {
    if !KEYBOARD_INITIALIZED.load(Ordering::SeqCst) {
        return -1;
    }
    
    unsafe {
        if let Some(ref mut buffer) = KEYBOARD_BUFFER {
            if let Some(ch) = buffer.pop_front() {
                ch as i32
            } else {
                -1
            }
        } else {
            -1
        }
    }
}

pub fn clear_buffer() {
    if !KEYBOARD_INITIALIZED.load(Ordering::SeqCst) {
        return;
    }
    
    unsafe {
        if let Some(ref mut buffer) = KEYBOARD_BUFFER {
            buffer.clear();
        }
    }
}

pub fn buffer_has_data() -> bool {
    if !KEYBOARD_INITIALIZED.load(Ordering::SeqCst) {
        return false;
    }
    
    unsafe {
        if let Some(ref buffer) = KEYBOARD_BUFFER {
            !buffer.is_empty()
        } else {
            false
        }
    }
}

// C interface functions
#[no_mangle]
pub extern "C" fn rust_keyboard_get_char() -> i32 {
    get_char()
}

#[no_mangle]
pub extern "C" fn rust_keyboard_clear_buffer() {
    clear_buffer();
}

#[no_mangle]
pub extern "C" fn rust_keyboard_handle_interrupt() {
    handle_keyboard_interrupt();
}

#[no_mangle]
pub extern "C" fn rust_keyboard_put_scancode(_scancode: u8) {
    // TODO: Implement actual logic
}
