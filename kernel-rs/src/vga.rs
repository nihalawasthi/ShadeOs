use core::ptr;
use core::iter::Iterator;

const VGA_BUFFER: *mut u16 = 0xB8000 as *mut u16;
const VGA_WIDTH: usize = 80;
const VGA_HEIGHT: usize = 25;

static mut CURSOR_X: usize = 0;
static mut CURSOR_Y: usize = 0;
static mut CURRENT_COLOR: u8 = 0x0F; // White on black

extern "C" {
    fn serial_write(s: *const u8);
    fn outb(port: u16, value: u8);
}

pub fn rust_vga_init() {
    unsafe {
        serial_write(b"[VGA] Initializing VGA driver\n\0".as_ptr());
        clear();
        set_color(0x0F); // White on black
        enable_cursor(14, 15); // Enable cursor with default size
        update_cursor();
    }
}

pub fn clear() {
    unsafe {
        for i in 0..(VGA_WIDTH * VGA_HEIGHT) {
            ptr::write_volatile(VGA_BUFFER.add(i), 0x0F20); // Space with white on black
        }
        CURSOR_X = 0;
        CURSOR_Y = 0;
        update_cursor();
    }
}

pub fn set_color(color: u8) {
    unsafe {
        CURRENT_COLOR = color;
    }
}

pub fn print_char(c: u8) {
    unsafe {
        match c {
            b'\n' => {
                CURSOR_X = 0;
                CURSOR_Y += 1;
                if CURSOR_Y >= VGA_HEIGHT {
                    scroll_up();
                    CURSOR_Y = VGA_HEIGHT - 1;
                }
                update_cursor();
            },
            b'\r' => {
                CURSOR_X = 0;
                update_cursor();
            },
            8 => { // Backspace
                if CURSOR_X > 0 {
                    CURSOR_X -= 1;
                    let pos = CURSOR_Y * VGA_WIDTH + CURSOR_X;
                    // Add bounds check
                    if pos < VGA_WIDTH * VGA_HEIGHT {
                        ptr::write_volatile(VGA_BUFFER.add(pos), (CURRENT_COLOR as u16) << 8 | b' ' as u16);
                    }
                    update_cursor();
                }
            },
            _ => {
                if CURSOR_X >= VGA_WIDTH {
                    CURSOR_X = 0;
                    CURSOR_Y += 1;
                    if CURSOR_Y >= VGA_HEIGHT {
                        scroll_up();
                        CURSOR_Y = VGA_HEIGHT - 1;
                    }
                }
                
                // Ensure cursor is within bounds before writing
                if CURSOR_Y < VGA_HEIGHT && CURSOR_X < VGA_WIDTH {
                    let pos = CURSOR_Y * VGA_WIDTH + CURSOR_X;
                    // Double-check bounds
                    if pos < VGA_WIDTH * VGA_HEIGHT {
                        let entry = (CURRENT_COLOR as u16) << 8 | c as u16;
                        ptr::write_volatile(VGA_BUFFER.add(pos), entry);
                        CURSOR_X += 1;
                        update_cursor();
                    }
                }
            }
        }
    }
}

pub fn print_string(text: *const u8) {
    if text.is_null() {
        return;
    }
    
    unsafe {
        let mut i = 0;
        loop {
            let c = ptr::read(text.add(i));
            if c == 0 {
                break;
            }
            print_char(c);
            i += 1;
        }
    }
}

fn scroll_up() {
    unsafe {
        // Move all lines up by one
        for y in 1..VGA_HEIGHT {
            for x in 0..VGA_WIDTH {
                let src_pos = y * VGA_WIDTH + x;
                let dst_pos = (y - 1) * VGA_WIDTH + x;
                let entry = ptr::read_volatile(VGA_BUFFER.add(src_pos));
                ptr::write_volatile(VGA_BUFFER.add(dst_pos), entry);
            }
        }
        
        // Clear the last line
        for x in 0..VGA_WIDTH {
            let pos = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
            ptr::write_volatile(VGA_BUFFER.add(pos), (CURRENT_COLOR as u16) << 8 | b' ' as u16);
        }
    }
}

pub fn print_hex(value: u64) {
    let hex_chars = b"0123456789ABCDEF";
    print_string(b"0x\0".as_ptr());
    
    for i in (0..16).rev() {
        let digit = ((value >> (i * 4)) & 0xF) as usize;
        print_char(hex_chars[digit]);
    }
}

pub fn print_dec(value: u64) {
    if value == 0 {
        print_char(b'0');
        return;
    }
    
    let mut digits = [0u8; 20];
    let mut num = value;
    let mut count = 0;
    
    while num > 0 {
        digits[count] = b'0' + (num % 10) as u8;
        num /= 10;
        count += 1;
    }
    
    for i in (0..count).rev() {
        print_char(digits[i]);
    }
}

// Cursor control functions
pub fn enable_cursor(cursor_start: u8, cursor_end: u8) {
    unsafe {
        outb(0x3D4, 0x0A);
        outb(0x3D5, (cursor_start & 0x1F));
        
        outb(0x3D4, 0x0B);
        outb(0x3D5, (cursor_end & 0x1F));
    }
}

pub fn disable_cursor() {
    unsafe {
        outb(0x3D4, 0x0A);
        outb(0x3D5, 0x20);
    }
}

pub fn update_cursor() {
    unsafe {
        let pos = CURSOR_Y * VGA_WIDTH + CURSOR_X;
        
        outb(0x3D4, 0x0F);
        outb(0x3D5, (pos & 0xFF) as u8);
        outb(0x3D4, 0x0E);
        outb(0x3D5, ((pos >> 8) & 0xFF) as u8);
    }
}

pub fn get_cursor_position() -> (usize, usize) {
    unsafe { (CURSOR_X, CURSOR_Y) }
}

pub fn set_cursor_position(x: usize, y: usize) {
    unsafe {
        CURSOR_X = x.min(VGA_WIDTH - 1);
        CURSOR_Y = y.min(VGA_HEIGHT - 1);
        update_cursor();
    }
}

// C interface functions
#[no_mangle]
pub extern "C" fn vga_print(text: *const u8) {
    print_string(text);
}

#[no_mangle]
pub extern "C" fn vga_putchar(c: u8) {
    print_char(c);
}

#[no_mangle]
pub extern "C" fn vga_clear() {
    clear();
}

#[no_mangle]
pub extern "C" fn vga_set_color(color: u8) {
    set_color(color);
}

#[no_mangle]
pub extern "C" fn rust_vga_clear() {
    clear();
}

#[no_mangle]
pub extern "C" fn rust_vga_set_color(color: u8) {
    set_color(color);
}

#[no_mangle]
pub extern "C" fn rust_vga_print(s: *const u8) {
    if s.is_null() {
        return;
    }
    unsafe {
        let mut i = 0;
        loop {
            let ch = *s.add(i);
            if ch == 0 { break; }
            print_char(ch);
            i += 1;
            // Safety check to prevent infinite loops with corrupted strings
            if i > 4096 { break; }
        }
    }
}

#[no_mangle]
pub extern "C" fn vga_enable_cursor(cursor_start: u8, cursor_end: u8) {
    enable_cursor(cursor_start, cursor_end);
}

#[no_mangle]
pub extern "C" fn vga_disable_cursor() {
    disable_cursor();
}

#[no_mangle]
pub extern "C" fn vga_update_cursor() {
    update_cursor();
}

#[no_mangle]
pub extern "C" fn vga_set_cursor_position(x: usize, y: usize) {
    set_cursor_position(x, y);
}
