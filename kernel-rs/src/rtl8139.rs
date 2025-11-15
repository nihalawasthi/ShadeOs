// RTL8139 Network Driver for ShadeOS
#![allow(dead_code)]

use core::ptr::{read_volatile, write_volatile};
use spin::Mutex;
use alloc::vec::Vec;

// RTL8139 Register Offsets
const REG_MAC0: u16 = 0x00;
const REG_MAR0: u16 = 0x08;
const REG_TSD0: u16 = 0x10;
const REG_TSAD0: u16 = 0x20;
const REG_RBSTART: u16 = 0x30;
const REG_CMD: u16 = 0x37;
const REG_CAPR: u16 = 0x38;
const REG_IMR: u16 = 0x3C;
const REG_ISR: u16 = 0x3E;
const REG_TCR: u16 = 0x40;
const REG_RCR: u16 = 0x44;
const REG_CONFIG1: u16 = 0x52;

// Command Register bits
const CMD_RESET: u8 = 0x10;
const CMD_RX_ENABLE: u8 = 0x08;
const CMD_TX_ENABLE: u8 = 0x04;

// Interrupt Status/Mask bits
const INT_ROK: u16 = 0x01; // Receive OK
const INT_TOK: u16 = 0x04; // Transmit OK
const INT_RER: u16 = 0x02; // Receive Error
const INT_TER: u16 = 0x08; // Transmit Error

// Receive Configuration
const RCR_AAP: u32 = 0x01; // Accept all packets
const RCR_APM: u32 = 0x02; // Accept physical match
const RCR_AM: u32 = 0x04;  // Accept multicast
const RCR_AB: u32 = 0x08;  // Accept broadcast
const RCR_WRAP: u32 = 0x80; // Wrap at end of buffer
const RCR_RBLEN_8K: u32 = 0x00; // 8K+16 RX buffer (bits 11-12 = 00)
const RCR_RBLEN_16K: u32 = 0x01 << 11; // 16K+16 RX buffer
const RCR_RBLEN_32K: u32 = 0x02 << 11; // 32K+16 RX buffer
const RCR_RBLEN_64K: u32 = 0x03 << 11; // 64K+16 RX buffer

// Transmit Configuration
const TCR_IFG: u32 = 0x03000000; // Interframe gap

// Buffer sizes
const RX_BUFFER_SIZE: usize = 8192 + 16 + 1500;
const TX_BUFFER_SIZE: usize = 1536;

extern "C" {
    fn rust_kmalloc(size: usize) -> *mut u8;
    fn rust_kfree(ptr: *mut u8);
    fn pmm_alloc_page() -> u64;
    fn pmm_free_page(addr: u64);
    fn serial_write(s: *const u8);
    fn serial_write_dec(s: *const u8, n: u64);
    fn serial_write_str(s: *const u8);
}

pub struct Rtl8139Device {
    io_base: u16,
    mac_address: [u8; 6],
    rx_buffer: *mut u8,
    tx_buffers: [*mut u8; 4],
    current_tx: usize,
    rx_offset: u16,
}

unsafe impl Send for Rtl8139Device {}
unsafe impl Sync for Rtl8139Device {}

impl Rtl8139Device {
    pub fn new(io_base: u16) -> Option<Self> {
        unsafe {
            // Allocate RX buffer
            let rx_buffer = rust_kmalloc(RX_BUFFER_SIZE);
            if rx_buffer.is_null() {
                return None;
            }

            // Allocate TX buffers
            let mut tx_buffers = [core::ptr::null_mut(); 4];
            for i in 0..4 {
                tx_buffers[i] = rust_kmalloc(TX_BUFFER_SIZE);
                if tx_buffers[i].is_null() {
                    // Cleanup on failure
                    rust_kfree(rx_buffer);
                    for j in 0..i {
                        rust_kfree(tx_buffers[j]);
                    }
                    return None;
                }
            }

            let mut device = Rtl8139Device {
                io_base,
                mac_address: [0; 6],
                rx_buffer,
                tx_buffers,
                current_tx: 0,
                rx_offset: 0,
            };

            device.init();
            Some(device)
        }
    }

    fn outb(&self, offset: u16, value: u8) {
        unsafe {
            let port = self.io_base + offset;
            core::arch::asm!(
                "out dx, al",
                in("dx") port,
                in("al") value,
                options(nomem, nostack, preserves_flags)
            );
        }
    }

    fn outw(&self, offset: u16, value: u16) {
        unsafe {
            let port = self.io_base + offset;
            core::arch::asm!(
                "out dx, ax",
                in("dx") port,
                in("ax") value,
                options(nomem, nostack, preserves_flags)
            );
        }
    }

    fn outl(&self, offset: u16, value: u32) {
        unsafe {
            let port = self.io_base + offset;
            core::arch::asm!(
                "out dx, eax",
                in("dx") port,
                in("eax") value,
                options(nomem, nostack, preserves_flags)
            );
        }
    }

    fn inb(&self, offset: u16) -> u8 {
        unsafe {
            let port = self.io_base + offset;
            let value: u8;
            core::arch::asm!(
                "in al, dx",
                in("dx") port,
                out("al") value,
                options(nomem, nostack, preserves_flags)
            );
            value
        }
    }

    fn inw(&self, offset: u16) -> u16 {
        unsafe {
            let port = self.io_base + offset;
            let value: u16;
            core::arch::asm!(
                "in ax, dx",
                in("dx") port,
                out("ax") value,
                options(nomem, nostack, preserves_flags)
            );
            value
        }
    }

    fn inl(&self, offset: u16) -> u32 {
        unsafe {
            let port = self.io_base + offset;
            let value: u32;
            core::arch::asm!(
                "in eax, dx",
                in("dx") port,
                out("eax") value,
                options(nomem, nostack, preserves_flags)
            );
            value
        }
    }

    fn init(&mut self) {
        unsafe {
            serial_write(b"[RTL8139] Initializing network device...\n\0".as_ptr());

            // Power on
            self.outb(REG_CONFIG1, 0x00);

            // Software reset
            serial_write(b"[RTL8139] Performing software reset...\n\0".as_ptr());
            self.outb(REG_CMD, CMD_RESET);
            while (self.inb(REG_CMD) & CMD_RESET) != 0 {
                core::hint::spin_loop();
            }
            serial_write(b"[RTL8139] Reset complete\n\0".as_ptr());

            // Read MAC address
            for i in 0..6 {
                self.mac_address[i] = self.inb(REG_MAC0 + i as u16);
            }
            serial_write(b"[RTL8139] MAC address read\n\0".as_ptr());

            // Set RX buffer
            serial_write(b"[RTL8139] Setting RX buffer addr=\0".as_ptr());
            serial_write_dec(b"\n\0".as_ptr(), self.rx_buffer as u64);
            self.outl(REG_RBSTART, self.rx_buffer as u32);
            
            // Verify RX buffer was set
            let rb_verify = self.inl(REG_RBSTART);
            serial_write(b"[RTL8139] RX buffer verify read back=\0".as_ptr());
            serial_write_dec(b"\n\0".as_ptr(), rb_verify as u64);
            
            // Reset CAPR (Current Address of Packet Read) - CRITICAL!
            serial_write(b"[RTL8139] Resetting CAPR\n\0".as_ptr());
            self.outw(REG_CAPR, 0xFFF0); // Start at beginning, -0x10 offset

            // Set IMR + ISR
            serial_write(b"[RTL8139] Configuring interrupts\n\0".as_ptr());
            self.outw(REG_IMR, INT_ROK | INT_TOK | INT_RER | INT_TER);
            self.outw(REG_ISR, 0xFFFF); // Clear all interrupts

            // Configure receive - use 8K+16 buffer size
            serial_write(b"[RTL8139] Configuring RX\n\0".as_ptr());
            self.outl(REG_RCR, RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_WRAP | RCR_RBLEN_8K);

            // Configure transmit
            serial_write(b"[RTL8139] Configuring TX\n\0".as_ptr());
            self.outl(REG_TCR, TCR_IFG);

            // Enable RX and TX
            serial_write(b"[RTL8139] Enabling RX and TX\n\0".as_ptr());
            self.outb(REG_CMD, CMD_RX_ENABLE | CMD_TX_ENABLE);
            
            // Verify command register
            let cmd_verify = self.inb(REG_CMD);
            serial_write(b"[RTL8139] CMD register=\0".as_ptr());
            serial_write_dec(b"\n\0".as_ptr(), cmd_verify as u64);

            serial_write(b"[RTL8139] Device initialized successfully\n\0".as_ptr());
        }
    }

    pub fn get_mac_address(&self) -> [u8; 6] {
        self.mac_address
    }

    pub fn transmit(&mut self, data: &[u8]) -> Result<(), &'static str> {
        if data.len() > TX_BUFFER_SIZE {
            return Err("Packet too large");
        }

        unsafe {
            let tx_buffer = self.tx_buffers[self.current_tx];
            core::ptr::copy_nonoverlapping(data.as_ptr(), tx_buffer, data.len());

            let tsd_offset = REG_TSD0 + (self.current_tx as u16 * 4);
            let tsad_offset = REG_TSAD0 + (self.current_tx as u16 * 4);

            // Set transmit address
            self.outl(tsad_offset, tx_buffer as u32);

            // Set transmit status (length)
            self.outl(tsd_offset, data.len() as u32);

            self.current_tx = (self.current_tx + 1) % 4;
        }

        Ok(())
    }

    pub fn receive(&mut self) -> Option<Vec<u8>> {
        unsafe {
            let cmd = self.inb(REG_CMD);
            if (cmd & 0x01) != 0 {
                // Buffer empty
                return None;
            }

            // Get current buffer read pointer (CAPR) - this is where we last read to
            let capr = self.inw(REG_CAPR);
            // Current buffer write pointer - where new data is being written
            let cbr_raw = self.inw(0x3A); // CBR register  

            serial_write(b"[RTL8139] RX: rx_offset=\0".as_ptr());
            serial_write_dec(b", CAPR=\0".as_ptr(), self.rx_offset as u64);
            serial_write_dec(b", CBR=\0".as_ptr(), capr as u64);
            serial_write_dec(b"\n\0".as_ptr(), cbr_raw as u64);

            let offset = self.rx_offset as usize;
            
            // Ensure offset is within buffer bounds
            if offset >= RX_BUFFER_SIZE {
                serial_write(b"[RTL8139] ERROR: rx_offset out of bounds, resetting\n\0".as_ptr());
                self.rx_offset = 0;
                self.outw(REG_CAPR, 0xFFF0);
                return None;
            }
            
            let rx_ptr = self.rx_buffer.add(offset);

            // Read header (4 bytes: 2 bytes status, 2 bytes length)
            let header = read_volatile(rx_ptr as *const u32);
            let status = (header & 0xFFFF) as u16;
            let length = ((header >> 16) & 0xFFFF) as u16;

            // Debug: show raw header
            serial_write(b"[RTL8139] RX Header: status=0x\0".as_ptr());
            serial_write_dec(b", length=\0".as_ptr(), status as u64);
            serial_write_dec(b" (includes 4-byte CRC)\n\0".as_ptr(), length as u64);

            // Validate packet status (bit 0 should be set for good packet)
            if (status & 0x01) == 0 {
                serial_write(b"[RTL8139] Bad packet status, skipping\n\0".as_ptr());
                // Skip this packet - align to 4 bytes and update CAPR
                let packet_len = if length > 0 { length } else { 4 };
                self.rx_offset = ((self.rx_offset + packet_len + 4 + 3) & !3) & 0xFFFF;
                if self.rx_offset >= (RX_BUFFER_SIZE as u16) {
                    self.rx_offset = 0;
                }
                self.outw(REG_CAPR, self.rx_offset.wrapping_sub(0x10));
                return None;
            }

            // The length field includes the 4-byte CRC at the end
            // Packet format: [Header 4 bytes][Ethernet Frame][CRC 4 bytes]
            // We want to return just the Ethernet frame without the CRC
            if length < 4 || length > 1518 {
                serial_write(b"[RTL8139] Invalid packet length=\0".as_ptr());
                serial_write_dec(b", skipping\n\0".as_ptr(), length as u64);
                // Skip malformed packet
                self.rx_offset = ((self.rx_offset + length + 4 + 3) & !3) & 0xFFFF;
                if self.rx_offset >= (RX_BUFFER_SIZE as u16) {
                    self.rx_offset = 0;
                }
                self.outw(REG_CAPR, self.rx_offset.wrapping_sub(0x10));
                return None;
            }

            // Copy packet data excluding the 4-byte CRC
            // RTL8139 packet format in RX buffer:
            // [Status:2 bytes][Length:2 bytes][Ethernet frame][CRC: 4 bytes]
            // The Length field is the size of [Ethernet frame + CRC], NOT including the 4-byte header
            let data_len = (length - 4) as usize; // Subtract CRC only
            
            // Debug: show what we're about to extract
            serial_write(b"[RTL8139] Extracting from offset \0".as_ptr());
            serial_write_dec(b", length=\0".as_ptr(), (offset + 4) as u64);
            serial_write_dec(b", data_len (excl CRC)=\0".as_ptr(), length as u64);
            serial_write_dec(b"\n\0".as_ptr(), data_len as u64);
            
            let packet_data = core::slice::from_raw_parts(rx_ptr.add(4), data_len);
            
            // Debug: dump first 20 bytes of extracted packet
            serial_write(b"[RTL8139] Extracted packet (first 20 bytes): \0".as_ptr());
            for i in 0..core::cmp::min(20, data_len) {
                let b = packet_data[i];
                fn nib(n: u8) -> u8 {
                    if n < 10 { b'0' + n } else { b'a' + (n - 10) }
                }
                let hi = nib((b >> 4) & 0xF);
                let lo = nib(b & 0xF);
                let mut buf = [hi, lo, b' ', 0u8];
                serial_write(buf.as_ptr());
            }
            serial_write(b"\n\0".as_ptr());
            
            let mut packet = Vec::with_capacity(data_len);
            packet.extend_from_slice(packet_data);

            // Update read pointer - align to 4-byte boundary
            // The +4 accounts for the header, +3 & !3 aligns to 4 bytes
            let new_offset = ((self.rx_offset + length + 4 + 3) & !3) & 0xFFFF;
            self.rx_offset = if new_offset >= (RX_BUFFER_SIZE as u16) {
                0 // Wrap around
            } else {
                new_offset
            };
            
            // Update CAPR register (Current Address of Packet Read)
            // CAPR should be (current_offset - 0x10) to account for the weird RTL8139 behavior
            self.outw(REG_CAPR, self.rx_offset.wrapping_sub(0x10));

            serial_write(b"[RTL8139] Packet RX, len=\0".as_ptr());
            serial_write_dec(b"\n\0".as_ptr(), packet.len() as u64);
            Some(packet)
        }
    }

    pub fn handle_interrupt(&mut self) {
        unsafe {
        serial_write(b"[RTL8139] handle_interrupt() called\n\0".as_ptr());
        let isr = self.inw(REG_ISR);
        serial_write(b"[RTL8139] ISR value=\0".as_ptr());
        serial_write_dec(b"\n\0".as_ptr(), isr as u64);
        // Clear interrupts
        self.outw(REG_ISR, isr);

        if (isr & INT_ROK) != 0 {
            serial_write(b"[RTL8139] INT_ROK: Packet received interrupt\n\0".as_ptr());
        }

        if (isr & INT_TOK) != 0 {
            serial_write(b"[RTL8139] INT_TOK: Packet transmitted interrupt\n\0".as_ptr());
        }
    }
}
}

impl Drop for Rtl8139Device {
    fn drop(&mut self) {
        unsafe {
            rust_kfree(self.rx_buffer);
            for i in 0..4 {
                rust_kfree(self.tx_buffers[i]);
            }
        }
    }
}

static mut RTL8139_DEVICE: Option<Rtl8139Device> = None;

#[no_mangle]
pub extern "C" fn rtl8139_init(io_base: u16) -> i32 {
    unsafe {
        match Rtl8139Device::new(io_base) {
            Some(device) => {
                RTL8139_DEVICE = Some(device);
                0
            }
            None => -1,
        }
    }
}

#[no_mangle]
pub extern "C" fn rtl8139_get_mac(mac_out: *mut u8) {
    unsafe {
        if let Some(ref device) = RTL8139_DEVICE {
            let mac = device.get_mac_address();
            core::ptr::copy_nonoverlapping(mac.as_ptr(), mac_out, 6);
        }
    }
}

#[no_mangle]
pub extern "C" fn rtl8139_transmit(data: *const u8, len: usize) -> i32 {
    unsafe {
        if let Some(ref mut device) = RTL8139_DEVICE {
            let slice = core::slice::from_raw_parts(data, len);
            match device.transmit(slice) {
                Ok(_) => 0,
                Err(_) => -1,
            }
        } else {
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn rtl8139_receive(buffer: *mut u8, max_len: usize) -> isize {
    unsafe {
        if let Some(ref mut device) = RTL8139_DEVICE {
            if let Some(packet) = device.receive() {
                let copy_len = core::cmp::min(packet.len(), max_len);
                core::ptr::copy_nonoverlapping(packet.as_ptr(), buffer, copy_len);
                return copy_len as isize;
            }
        }
        -1
    }
}

#[no_mangle]
pub extern "C" fn rtl8139_handle_interrupt() {
    unsafe {
        if let Some(ref mut device) = RTL8139_DEVICE {
            device.handle_interrupt();
        }
    }
}
