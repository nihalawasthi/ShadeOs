// Canonical serial port implementation for both C and Rust
// Exports serial_init, serial_putchar, serial_write as #[no_mangle] extern "C"
// Uses FFI to inb/outb from kernel C code

extern "C" {
    fn inb(port: u16) -> u8;
    fn outb(port: u16, data: u8);
}

const COM1_PORT: u16 = 0x3F8;

#[no_mangle]
pub extern "C" fn serial_init() {
    unsafe {
        outb(COM1_PORT + 1, 0x00);    // Disable all interrupts
        outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
        outb(COM1_PORT + 0, 0x01);    // Set divisor to 1 (lo byte) 115200 baud
        outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
        outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
        outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear them, 14-byte threshold
        outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    }
}

#[no_mangle]
pub extern "C" fn serial_putchar(c: u8) {
    unsafe {
        while (inb(COM1_PORT + 5) & 0x20) == 0 {}
        outb(COM1_PORT, c);
    }
}

#[no_mangle]
pub extern "C" fn serial_write(s: *const u8) {
    if s.is_null() { return; }
    unsafe {
        let mut ptr = s;
        loop {
            let ch = *ptr;
            if ch == 0 { break; }
            serial_putchar(ch);
            ptr = ptr.add(1);
        }
    }
}

#[no_mangle]
pub extern "C" fn serial_write_hex(label: *const u8, value: u64) {
    // Print label
    serial_write(label);
    // Print 0x and hex digits
    let mut buf = [0u8; 18];
    buf[0] = b'0'; buf[1] = b'x';
    for i in 0..16 {
        let shift = 60 - i * 4;
        let digit = ((value >> shift) & 0xF) as u8;
        buf[2 + i] = if digit < 10 { b'0' + digit } else { b'A' + (digit - 10) };
    }
    buf[17] = 0;
    serial_write(buf.as_ptr());
    serial_write(b"\n\0".as_ptr());
}

#[no_mangle]
pub extern "C" fn serial_write_dec(label: *const u8, value: u64) {
    serial_write(label);
    let mut buf = [0u8; 32];
    let mut i = 30;
    buf[31] = 0;
    let mut val = value;
    if val == 0 {
        buf[i] = b'0';
        i -= 1;
    } else {
        while val > 0 && i > 0 {
            buf[i] = b'0' + (val % 10) as u8;
            val /= 10;
            i -= 1;
        }
    }
    serial_write(buf[i+1..].as_ptr());
    serial_write(b"\n\0".as_ptr());
} 