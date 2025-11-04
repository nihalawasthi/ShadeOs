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
            self.outb(REG_CMD, CMD_RESET);
            while (self.inb(REG_CMD) & CMD_RESET) != 0 {
                core::hint::spin_loop();
            }

            // Read MAC address
            for i in 0..6 {
                self.mac_address[i] = self.inb(REG_MAC0 + i as u16);
            }

            // Set RX buffer
            self.outl(REG_RBSTART, self.rx_buffer as u32);

            // Set IMR + ISR
            self.outw(REG_IMR, INT_ROK | INT_TOK | INT_RER | INT_TER);
            self.outw(REG_ISR, 0xFFFF); // Clear all interrupts

            // Configure receive
            self.outl(REG_RCR, RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_WRAP);

            // Configure transmit
            self.outl(REG_TCR, TCR_IFG);

            // Enable RX and TX
            self.outb(REG_CMD, CMD_RX_ENABLE | CMD_TX_ENABLE);

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

            let offset = self.rx_offset as usize;
            let rx_ptr = self.rx_buffer.add(offset);

            // Read header (4 bytes)
            let header = read_volatile(rx_ptr as *const u32);
            let status = (header & 0xFFFF) as u16;
            let length = ((header >> 16) & 0xFFFF) as u16;

            if (status & 0x01) == 0 {
                // Not a good packet
                self.rx_offset = (self.rx_offset + length + 4 + 3) & !3;
                self.outw(REG_CAPR, self.rx_offset.wrapping_sub(0x10));
                return None;
            }

            // Copy packet data
            let packet_data = core::slice::from_raw_parts(rx_ptr.add(4), length as usize - 4);
            let mut packet = Vec::with_capacity(packet_data.len());
            packet.extend_from_slice(packet_data);

            // Update read pointer
            self.rx_offset = ((self.rx_offset + length + 4 + 3) & !3) % RX_BUFFER_SIZE as u16;
            self.outw(REG_CAPR, self.rx_offset.wrapping_sub(0x10));

            Some(packet)
        }
    }

    pub fn handle_interrupt(&mut self) {
        let isr = self.inw(REG_ISR);
        
        // Clear interrupts
        self.outw(REG_ISR, isr);

        if (isr & INT_ROK) != 0 {
            // Packet received - will be handled by polling
        }

        if (isr & INT_TOK) != 0 {
            // Packet transmitted
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
