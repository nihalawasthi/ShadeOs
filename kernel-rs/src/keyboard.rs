use core::sync::atomic::{AtomicUsize, Ordering};
use core::cell::UnsafeCell;

// External C functions for logging
extern "C" {
    fn vga_print(s: *const u8);
    fn serial_write(s: *const u8);
    fn sys_cli(); // Disable interrupts
    fn sys_sti(); // Enable interrupts
    fn pause();   // HLT instruction
}

// QWERTY scancode -> ASCII tables (from C keyboard.c)
const LOWER_ASCII_CODES: [u8; 92] = [
    0x00, 27, b'1', b'2',     /* 0x00 */
    b'3', b'4', b'5', b'6',     /* 0x04 */
    b'7', b'8', b'9', b'0',     /* 0x08 */
    b'-', b'=', 8, b'\t',     /* 0x0C */
    b'q', b'w', b'e', b'r',     /* 0x10 */
    b't', b'y', b'u', b'i',     /* 0x14 */
    b'o', b'p', b'[', b']',     /* 0x18 */
    b'\n', 0x00, b'a', b's',     /* 0x1C */
    b'd', b'f', b'g', b'h',     /* 0x20 */
    b'j', b'k', b'l', b';',     /* 0x24 */
    b'\'', b'`', 0x00, b'\\',     /* 0x28 */
    b'z', b'x', b'c', b'v',     /* 0x2C */
    b'b', b'n', b'm', b',',     /* 0x30 */
    b'.', b'/', 0x00, b'*',     /* 0x34 */
    0x00, b' ', 0x00, 0x00,     /* 0x38 */
    0x00, 0x00, 0x00, 0x00,     /* 0x3C */
    0x00, 0x00, 0x00, 0x00,     /* 0x40 */
    0x00, 0x00, 0x00, b'7',     /* 0x44 */
    b'8', b'9', b'-', b'4',     /* 0x48 */
    b'5', b'6', b'+', b'1',     /* 0x4C */
    b'2', b'3', b'0', b'.',     /* 0x50 */
    0x00, 0x00, 0x00, 0x00,     /* 0x54 */
    0x00, 0x00, 0x00, 0x00      /* 0x58 */
];
const UPPER_ASCII_CODES: [u8; 92] = [
    0x00, 27, b'!', b'@',     /* 0x00 */
    b'#', b'$', b'%', b'^',     /* 0x04 */
    b'&', b'*', b'(', b')',     /* 0x08 */
    b'_', b'+', 8, b'\t',     /* 0x0C */
    b'Q', b'W', b'E', b'R',     /* 0x10 */
    b'T', b'Y', b'U', b'I',     /* 0x14 */
    b'O', b'P', b'{', b'}',     /* 0x18 */
    b'\n', 0x00, b'A', b'S',     /* 0x1C */
    b'D', b'F', b'G', b'H',     /* 0x20 */
    b'J', b'K', b'L', b':',     /* 0x24 */
    b'"', b'~', 0x00, b'|',     /* 0x28 */
    b'Z', b'X', b'C', b'V',     /* 0x2C */
    b'B', b'N', b'M', b'<',     /* 0x30 */
    b'>', b'?', 0x00, b'*',     /* 0x34 */
    0x00, b' ', 0x00, 0x00,     /* 0x38 */
    0x00, 0x00, 0x00, 0x00,     /* 0x3C */
    0x00, 0x00, 0x00, 0x00,     /* 0x40 */
    0x00, 0x00, 0x00, 0x00,     /* 0x44 */
    0x00, 0x00, 0x00, 0x00,     /* 0x48 */
    0x00, 0x00, 0x00, 0x00,     /* 0x4C */
    0x00, 0x00, 0x00, 0x00,     /* 0x50 */
    0x00, 0x00, 0x00, 0x00,     /* 0x54 */
    0x00, 0x00, 0x00, 0x00      /* 0x58 */
];

const BUFFLEN: usize = 128;

struct KeyboardBuffer {
    buffer: [u8; BUFFLEN],
    head: AtomicUsize,
    tail: AtomicUsize,
    shift: UnsafeCell<u8>,
    ctrl: UnsafeCell<u8>,
    capslock: UnsafeCell<i32>,
    keypresses: UnsafeCell<[u8; 256]>,
}

// Use a static instance of the keyboard buffer
static mut KEYBOARD_BUFFER: KeyboardBuffer = KeyboardBuffer {
    buffer: [0; BUFFLEN],
    head: AtomicUsize::new(0),
    tail: AtomicUsize::new(0),
    shift: UnsafeCell::new(0),
    ctrl: UnsafeCell::new(0),
    capslock: UnsafeCell::new(0),
    keypresses: UnsafeCell::new([0; 256]),
};

pub fn init() {
    unsafe {
        serial_write(b"[RUST KB] Keyboard buffer initialized\n\0".as_ptr());
        vga_print(b"[RUST KB] Keyboard buffer initialized\n\0".as_ptr());
    }
}

#[no_mangle]
pub extern "C" fn rust_keyboard_put_scancode(scancode: u8) {
    unsafe {
        sys_cli(); // Disable interrupts for critical section

        let head = KEYBOARD_BUFFER.head.load(Ordering::Relaxed);
        let tail = KEYBOARD_BUFFER.tail.load(Ordering::Relaxed);
        let next_head = (head + 1) % BUFFLEN;

        if next_head == tail {
            // Buffer full, drop character
            sys_sti();
            return;
        }

        let shift = KEYBOARD_BUFFER.shift.get();
        let ctrl = KEYBOARD_BUFFER.ctrl.get();
        let capslock = KEYBOARD_BUFFER.capslock.get();
        let keypresses = KEYBOARD_BUFFER.keypresses.get();

        if scancode & 0x80 != 0 {
            // Key release
            let pressed_scancode = scancode & 0x7F;
            if pressed_scancode == 0x2A { // Left Shift
                *shift &= !0x01;
            } else if pressed_scancode == 0x36 { // Right Shift
                *shift &= !0x02;
            } else if pressed_scancode == 0x1D { // Control
                *ctrl = 0;
            }
            (*keypresses)[pressed_scancode as usize] = 0;
            sys_sti();
            return;
        }

        // Key press
        if (*keypresses)[scancode as usize] > 0 && (*keypresses)[scancode as usize] < 10 {
            // Key already pressed, ignore repeat for now
            (*keypresses)[scancode as usize] += 1;
            sys_sti();
            return;
        }
        (*keypresses)[scancode as usize] += 1;

        if scancode == 0x2A { // Left Shift
            *shift |= 0x01;
            sys_sti();
            return;
        } else if scancode == 0x36 { // Right Shift
            *shift |= 0x02;
            sys_sti();
            return;
        } else if scancode == 0x1D { // Control
            *ctrl = 1;
            sys_sti();
            return;
        } else if scancode == 0x3A { // Caps Lock
            *capslock = if *capslock == 0 { 1 } else { 0 };
            sys_sti();
            return;
        }

        let ascii_code: u8;
        let scancode_idx = scancode as usize;
        if scancode_idx >= LOWER_ASCII_CODES.len() {
            ascii_code = 0; // Out of bounds
        } else if *ctrl != 0 {
            if LOWER_ASCII_CODES[scancode_idx] == b'd' {
                // Ctrl+D (EOT)
                ascii_code = 4; // EOT
            } else {
                ascii_code = 0; // No ASCII for other Ctrl combinations
            }
        } else if *shift != 0 {
            ascii_code = if scancode_idx < UPPER_ASCII_CODES.len() { UPPER_ASCII_CODES[scancode_idx] } else { 0 };
        } else if *capslock != 0 && LOWER_ASCII_CODES[scancode_idx] >= b'a' && LOWER_ASCII_CODES[scancode_idx] <= b'z' {
            ascii_code = LOWER_ASCII_CODES[scancode_idx] - 32; // Convert to uppercase
        } else if *capslock != 0 && LOWER_ASCII_CODES[scancode_idx] >= b'A' && LOWER_ASCII_CODES[scancode_idx] <= b'Z' {
            ascii_code = LOWER_ASCII_CODES[scancode_idx] + 32; // Convert to lowercase
        } else {
            ascii_code = LOWER_ASCII_CODES[scancode_idx];
        }

        if ascii_code != 0 {
            KEYBOARD_BUFFER.buffer[head] = ascii_code;
            KEYBOARD_BUFFER.head.store(next_head, Ordering::Relaxed);
        }
        sys_sti(); // Re-enable interrupts
    }
}

#[no_mangle]
pub extern "C" fn rust_keyboard_get_char() -> i32 {
    unsafe {
        loop {
            sys_cli(); // Disable interrupts for critical section

            let head = KEYBOARD_BUFFER.head.load(Ordering::Relaxed);
            let tail = KEYBOARD_BUFFER.tail.load(Ordering::Relaxed);

            if head != tail {
                let c = KEYBOARD_BUFFER.buffer[tail];
                KEYBOARD_BUFFER.tail.store((tail + 1) % BUFFLEN, Ordering::Relaxed);
                sys_sti(); // Re-enable interrupts
                return c as i32;
            }

            sys_sti(); // Re-enable interrupts before halting
            pause(); // Halt CPU until next interrupt
        }
    }
}
