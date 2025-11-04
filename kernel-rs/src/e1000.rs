// Intel E1000 Network Driver for ShadeOS
#![allow(dead_code)]

use core::ptr::{read_volatile, write_volatile};
use alloc::vec::Vec;

// E1000 Register Offsets
const REG_CTRL: u32 = 0x00000;
const REG_STATUS: u32 = 0x00008;
const REG_EEPROM: u32 = 0x00014;
const REG_CTRL_EXT: u32 = 0x00018;
const REG_IMASK: u32 = 0x000D0;
const REG_RCTRL: u32 = 0x00100;
const REG_RXDESCLO: u32 = 0x02800;
const REG_RXDESCHI: u32 = 0x02804;
const REG_RXDESCLEN: u32 = 0x02808;
const REG_RXDESCHEAD: u32 = 0x02810;
const REG_RXDESCTAIL: u32 = 0x02818;
const REG_TCTRL: u32 = 0x00400;
const REG_TXDESCLO: u32 = 0x03800;
const REG_TXDESCHI: u32 = 0x03804;
const REG_TXDESCLEN: u32 = 0x03808;
const REG_TXDESCHEAD: u32 = 0x03810;
const REG_TXDESCTAIL: u32 = 0x03818;
const REG_RDTR: u32 = 0x02820;
const REG_RXDCTL: u32 = 0x03828;
const REG_RADV: u32 = 0x0282C;
const REG_RSRPD: u32 = 0x02C00;
const REG_TIPG: u32 = 0x00410;

// Control Register
const CTRL_FD: u32 = 0x00000001; // Full duplex
const CTRL_ASDE: u32 = 0x00000020; // Auto-speed detection
const CTRL_SLU: u32 = 0x00000040; // Set link up
const CTRL_RST: u32 = 0x04000000; // Device reset

// Receive Control Register
const RCTL_EN: u32 = 0x00000002; // Enable
const RCTL_SBP: u32 = 0x00000004; // Store bad packets
const RCTL_UPE: u32 = 0x00000008; // Unicast promiscuous
const RCTL_MPE: u32 = 0x00000010; // Multicast promiscuous
const RCTL_LPE: u32 = 0x00000020; // Long packet enable
const RCTL_LBM: u32 = 0x000000C0; // Loopback mode
const RCTL_BAM: u32 = 0x00008000; // Broadcast accept mode
const RCTL_BSIZE: u32 = 0x00030000; // Buffer size (2048 bytes)
const RCTL_BSEX: u32 = 0x02000000; // Buffer size extension
const RCTL_SECRC: u32 = 0x04000000; // Strip CRC

// Transmit Control Register
const TCTL_EN: u32 = 0x00000002; // Enable
const TCTL_PSP: u32 = 0x00000008; // Pad short packets
const TCTL_CT: u32 = 0x00000FF0; // Collision threshold
const TCTL_COLD: u32 = 0x003FF000; // Collision distance

// Descriptor status
const DESC_STATUS_DD: u8 = 0x01; // Descriptor done
const DESC_STATUS_EOP: u8 = 0x02; // End of packet

const NUM_RX_DESC: usize = 32;
const NUM_TX_DESC: usize = 32;

extern "C" {
    fn rust_kmalloc(size: usize) -> *mut u8;
    fn rust_kfree(ptr: *mut u8);
    fn serial_write(s: *const u8);
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct RxDescriptor {
    addr: u64,
    length: u16,
    checksum: u16,
    status: u8,
    errors: u8,
    special: u16,
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct TxDescriptor {
    addr: u64,
    length: u16,
    cso: u8,
    cmd: u8,
    status: u8,
    css: u8,
    special: u16,
}

pub struct E1000Device {
    mem_base: u64,
    mac_address: [u8; 6],
    rx_descs: *mut RxDescriptor,
    tx_descs: *mut TxDescriptor,
    rx_buffers: Vec<*mut u8>,
    tx_buffers: Vec<*mut u8>,
    rx_current: usize,
    tx_current: usize,
}

unsafe impl Send for E1000Device {}
unsafe impl Sync for E1000Device {}

impl E1000Device {
    pub fn new(mem_base: u64) -> Option<Self> {
        unsafe {
            // Allocate descriptor rings
            let rx_descs = rust_kmalloc(core::mem::size_of::<RxDescriptor>() * NUM_RX_DESC) as *mut RxDescriptor;
            let tx_descs = rust_kmalloc(core::mem::size_of::<TxDescriptor>() * NUM_TX_DESC) as *mut TxDescriptor;

            if rx_descs.is_null() || tx_descs.is_null() {
                if !rx_descs.is_null() {
                    rust_kfree(rx_descs as *mut u8);
                }
                if !tx_descs.is_null() {
                    rust_kfree(tx_descs as *mut u8);
                }
                return None;
            }

            // Allocate packet buffers
            let mut rx_buffers = Vec::new();
            let mut tx_buffers = Vec::new();

            for _ in 0..NUM_RX_DESC {
                let buf = rust_kmalloc(2048);
                if buf.is_null() {
                    // Cleanup on failure
                    for b in rx_buffers {
                        rust_kfree(b);
                    }
                    rust_kfree(rx_descs as *mut u8);
                    rust_kfree(tx_descs as *mut u8);
                    return None;
                }
                rx_buffers.push(buf);
            }

            for _ in 0..NUM_TX_DESC {
                let buf = rust_kmalloc(2048);
                if buf.is_null() {
                    // Cleanup on failure
                    for b in rx_buffers {
                        rust_kfree(b);
                    }
                    for b in tx_buffers {
                        rust_kfree(b);
                    }
                    rust_kfree(rx_descs as *mut u8);
                    rust_kfree(tx_descs as *mut u8);
                    return None;
                }
                tx_buffers.push(buf);
            }

            let mut device = E1000Device {
                mem_base,
                mac_address: [0; 6],
                rx_descs,
                tx_descs,
                rx_buffers,
                tx_buffers,
                rx_current: 0,
                tx_current: 0,
            };

            device.init();
            Some(device)
        }
    }

    fn read_reg(&self, offset: u32) -> u32 {
        unsafe {
            let addr = (self.mem_base + offset as u64) as *const u32;
            read_volatile(addr)
        }
    }

    fn write_reg(&self, offset: u32, value: u32) {
        unsafe {
            let addr = (self.mem_base + offset as u64) as *mut u32;
            write_volatile(addr, value);
        }
    }

    fn read_eeprom(&self, addr: u8) -> u16 {
        let mut data = 0u16;
        self.write_reg(REG_EEPROM, 1 | ((addr as u32) << 8));
        
        // Wait for read to complete
        for _ in 0..1000 {
            let val = self.read_reg(REG_EEPROM);
            if (val & 0x10) != 0 {
                data = ((val >> 16) & 0xFFFF) as u16;
                break;
            }
        }
        data
    }

    fn init(&mut self) {
        unsafe {
            serial_write(b"[E1000] Initializing Intel E1000 network device...\n\0".as_ptr());

            // Reset the device
            self.write_reg(REG_CTRL, self.read_reg(REG_CTRL) | CTRL_RST);
            
            // Wait for reset to complete
            for _ in 0..1000 {
                if (self.read_reg(REG_CTRL) & CTRL_RST) == 0 {
                    break;
                }
                core::hint::spin_loop();
            }

            // Read MAC address from EEPROM
            let mac0 = self.read_eeprom(0);
            let mac1 = self.read_eeprom(1);
            let mac2 = self.read_eeprom(2);

            self.mac_address[0] = (mac0 & 0xFF) as u8;
            self.mac_address[1] = ((mac0 >> 8) & 0xFF) as u8;
            self.mac_address[2] = (mac1 & 0xFF) as u8;
            self.mac_address[3] = ((mac1 >> 8) & 0xFF) as u8;
            self.mac_address[4] = (mac2 & 0xFF) as u8;
            self.mac_address[5] = ((mac2 >> 8) & 0xFF) as u8;

            // Set link up
            self.write_reg(REG_CTRL, self.read_reg(REG_CTRL) | CTRL_SLU | CTRL_ASDE);

            // Clear multicast array
            for i in 0..128 {
                self.write_reg(0x5200 + i * 4, 0);
            }

            // Clear interrupts
            self.write_reg(REG_IMASK, 0);
            self.read_reg(0xC0); // Clear interrupt causes

            // Setup RX descriptors
            for i in 0..NUM_RX_DESC {
                let desc = &mut *self.rx_descs.add(i);
                desc.addr = self.rx_buffers[i] as u64;
                desc.status = 0;
            }

            self.write_reg(REG_RXDESCLO, self.rx_descs as u32);
            self.write_reg(REG_RXDESCHI, ((self.rx_descs as u64) >> 32) as u32);
            self.write_reg(REG_RXDESCLEN, (NUM_RX_DESC * core::mem::size_of::<RxDescriptor>()) as u32);
            self.write_reg(REG_RXDESCHEAD, 0);
            self.write_reg(REG_RXDESCTAIL, (NUM_RX_DESC - 1) as u32);

            // Setup TX descriptors
            for i in 0..NUM_TX_DESC {
                let desc = &mut *self.tx_descs.add(i);
                desc.addr = self.tx_buffers[i] as u64;
                desc.status = DESC_STATUS_DD;
                desc.cmd = 0;
            }

            self.write_reg(REG_TXDESCLO, self.tx_descs as u32);
            self.write_reg(REG_TXDESCHI, ((self.tx_descs as u64) >> 32) as u32);
            self.write_reg(REG_TXDESCLEN, (NUM_TX_DESC * core::mem::size_of::<TxDescriptor>()) as u32);
            self.write_reg(REG_TXDESCHEAD, 0);
            self.write_reg(REG_TXDESCTAIL, 0);

            // Enable receiver
            self.write_reg(REG_RCTRL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | 
                          RCTL_LBM | RCTL_BAM | RCTL_BSIZE | RCTL_SECRC);

            // Enable transmitter
            self.write_reg(REG_TCTRL, TCTL_EN | TCTL_PSP | (15 << 4) | (64 << 12));
            self.write_reg(REG_TIPG, 0x0060200A);

            serial_write(b"[E1000] Device initialized successfully\n\0".as_ptr());
        }
    }

    pub fn get_mac_address(&self) -> [u8; 6] {
        self.mac_address
    }

    pub fn transmit(&mut self, data: &[u8]) -> Result<(), &'static str> {
        if data.len() > 2048 {
            return Err("Packet too large");
        }

        unsafe {
            let desc = &mut *self.tx_descs.add(self.tx_current);
            
            // Wait for descriptor to be free
            while (desc.status & DESC_STATUS_DD) == 0 {
                core::hint::spin_loop();
            }

            // Copy data to buffer
            core::ptr::copy_nonoverlapping(data.as_ptr(), self.tx_buffers[self.tx_current], data.len());

            // Setup descriptor
            desc.length = data.len() as u16;
            desc.cmd = 0x0B; // EOP | IFCS | RS
            desc.status = 0;

            // Update tail
            self.tx_current = (self.tx_current + 1) % NUM_TX_DESC;
            self.write_reg(REG_TXDESCTAIL, self.tx_current as u32);
        }

        Ok(())
    }

    pub fn receive(&mut self) -> Option<Vec<u8>> {
        unsafe {
            let desc = &mut *self.rx_descs.add(self.rx_current);

            if (desc.status & DESC_STATUS_DD) == 0 {
                return None;
            }

            let length = desc.length as usize;
            let mut packet = Vec::with_capacity(length);
            let data = core::slice::from_raw_parts(self.rx_buffers[self.rx_current], length);
            packet.extend_from_slice(data);

            // Reset descriptor
            desc.status = 0;

            // Update tail
            self.rx_current = (self.rx_current + 1) % NUM_RX_DESC;
            self.write_reg(REG_RXDESCTAIL, self.rx_current as u32);

            Some(packet)
        }
    }
}

impl Drop for E1000Device {
    fn drop(&mut self) {
        unsafe {
            for buf in &self.rx_buffers {
                rust_kfree(*buf);
            }
            for buf in &self.tx_buffers {
                rust_kfree(*buf);
            }
            rust_kfree(self.rx_descs as *mut u8);
            rust_kfree(self.tx_descs as *mut u8);
        }
    }
}

static mut E1000_DEVICE: Option<E1000Device> = None;

#[no_mangle]
pub extern "C" fn e1000_init(mem_base: u64) -> i32 {
    unsafe {
        match E1000Device::new(mem_base) {
            Some(device) => {
                E1000_DEVICE = Some(device);
                0
            }
            None => -1,
        }
    }
}

#[no_mangle]
pub extern "C" fn e1000_get_mac(mac_out: *mut u8) {
    unsafe {
        if let Some(ref device) = E1000_DEVICE {
            let mac = device.get_mac_address();
            core::ptr::copy_nonoverlapping(mac.as_ptr(), mac_out, 6);
        }
    }
}

#[no_mangle]
pub extern "C" fn e1000_transmit(data: *const u8, len: usize) -> i32 {
    unsafe {
        if let Some(ref mut device) = E1000_DEVICE {
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
pub extern "C" fn e1000_receive(buffer: *mut u8, max_len: usize) -> isize {
    unsafe {
        if let Some(ref mut device) = E1000_DEVICE {
            if let Some(packet) = device.receive() {
                let copy_len = core::cmp::min(packet.len(), max_len);
                core::ptr::copy_nonoverlapping(packet.as_ptr(), buffer, copy_len);
                return copy_len as isize;
            }
        }
        -1
    }
}
