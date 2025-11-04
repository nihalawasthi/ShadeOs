// AMD PCnet Network Driver for ShadeOS
#![allow(dead_code)]

use alloc::vec::Vec;

// PCnet Register Offsets
const REG_APROM: u16 = 0x00;
const REG_RDP: u16 = 0x10;
const REG_RAP: u16 = 0x12;
const REG_RESET: u16 = 0x14;
const REG_BDP: u16 = 0x16;

// CSR (Control and Status Register) numbers
const CSR_STATUS: u16 = 0;
const CSR_IADR0: u16 = 1;
const CSR_IADR1: u16 = 2;
const CSR_MODE: u16 = 15;
const CSR_RXADDR_LO: u16 = 24;
const CSR_RXADDR_HI: u16 = 25;
const CSR_TXADDR_LO: u16 = 30;
const CSR_TXADDR_HI: u16 = 31;

// BCR (Bus Control Register) numbers
const BCR_MC: u16 = 2;
const BCR_LED: u16 = 4;
const BCR_SWSTYLE: u16 = 20;

// Status bits
const STATUS_INIT: u16 = 0x0001;
const STATUS_STRT: u16 = 0x0002;
const STATUS_STOP: u16 = 0x0004;
const STATUS_TDMD: u16 = 0x0008;
const STATUS_TXON: u16 = 0x0010;
const STATUS_RXON: u16 = 0x0020;
const STATUS_INEA: u16 = 0x0040;
const STATUS_INTR: u16 = 0x0080;

const NUM_RX_DESC: usize = 32;
const NUM_TX_DESC: usize = 32;

extern "C" {
    fn rust_kmalloc(size: usize) -> *mut u8;
    fn rust_kfree(ptr: *mut u8);
    fn serial_write(s: *const u8);
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct InitBlock {
    mode: u16,
    reserved1: u8,
    rlen: u8,
    reserved2: u8,
    tlen: u8,
    padr: [u8; 6],
    reserved3: u16,
    ladr: [u8; 8],
    rdra: u32,
    tdra: u32,
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct RxDescriptor {
    addr: u32,
    flags: u16,
    buf_len: u16,
    msg_len: u16,
    reserved: u16,
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct TxDescriptor {
    addr: u32,
    flags: u16,
    buf_len: u16,
    reserved: u32,
}

pub struct PcnetDevice {
    io_base: u16,
    mac_address: [u8; 6],
    init_block: *mut InitBlock,
    rx_descs: *mut RxDescriptor,
    tx_descs: *mut TxDescriptor,
    rx_buffers: Vec<*mut u8>,
    tx_buffers: Vec<*mut u8>,
    rx_current: usize,
    tx_current: usize,
}

unsafe impl Send for PcnetDevice {}
unsafe impl Sync for PcnetDevice {}

impl PcnetDevice {
    pub fn new(io_base: u16) -> Option<Self> {
        unsafe {
            // Allocate init block
            let init_block = rust_kmalloc(core::mem::size_of::<InitBlock>()) as *mut InitBlock;
            if init_block.is_null() {
                return None;
            }

            // Allocate descriptor rings
            let rx_descs = rust_kmalloc(core::mem::size_of::<RxDescriptor>() * NUM_RX_DESC) as *mut RxDescriptor;
            let tx_descs = rust_kmalloc(core::mem::size_of::<TxDescriptor>() * NUM_TX_DESC) as *mut TxDescriptor;

            if rx_descs.is_null() || tx_descs.is_null() {
                rust_kfree(init_block as *mut u8);
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
                let buf = rust_kmalloc(1536);
                if buf.is_null() {
                    for b in rx_buffers {
                        rust_kfree(b);
                    }
                    rust_kfree(init_block as *mut u8);
                    rust_kfree(rx_descs as *mut u8);
                    rust_kfree(tx_descs as *mut u8);
                    return None;
                }
                rx_buffers.push(buf);
            }

            for _ in 0..NUM_TX_DESC {
                let buf = rust_kmalloc(1536);
                if buf.is_null() {
                    for b in rx_buffers {
                        rust_kfree(b);
                    }
                    for b in tx_buffers {
                        rust_kfree(b);
                    }
                    rust_kfree(init_block as *mut u8);
                    rust_kfree(rx_descs as *mut u8);
                    rust_kfree(tx_descs as *mut u8);
                    return None;
                }
                tx_buffers.push(buf);
            }

            let mut device = PcnetDevice {
                io_base,
                mac_address: [0; 6],
                init_block,
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

    fn read_csr(&self, csr: u16) -> u16 {
        self.outw(REG_RAP, csr);
        self.inw(REG_RDP)
    }

    fn write_csr(&self, csr: u16, value: u16) {
        self.outw(REG_RAP, csr);
        self.outw(REG_RDP, value);
    }

    fn read_bcr(&self, bcr: u16) -> u16 {
        self.outw(REG_RAP, bcr);
        self.inw(REG_BDP)
    }

    fn write_bcr(&self, bcr: u16, value: u16) {
        self.outw(REG_RAP, bcr);
        self.outw(REG_BDP, value);
    }

    fn init(&mut self) {
        unsafe {
            serial_write(b"[PCNET] Initializing AMD PCnet network device...\n\0".as_ptr());

            // Reset the device
            self.inw(REG_RESET);
            self.outw(REG_RESET, 0);

            // Read MAC address
            for i in 0..6 {
                self.mac_address[i] = (self.inw(REG_APROM + i as u16 * 2) & 0xFF) as u8;
            }

            // Set 32-bit mode
            self.write_bcr(BCR_SWSTYLE, 2);

            // Setup init block
            let ib = &mut *self.init_block;
            ib.mode = 0;
            ib.rlen = ((NUM_RX_DESC as u8).trailing_zeros() << 4) as u8;
            ib.tlen = ((NUM_TX_DESC as u8).trailing_zeros() << 4) as u8;
            ib.padr.copy_from_slice(&self.mac_address);
            ib.ladr = [0xFF; 8]; // Accept all multicast
            ib.rdra = self.rx_descs as u32;
            ib.tdra = self.tx_descs as u32;

            // Setup RX descriptors
            for i in 0..NUM_RX_DESC {
                let desc = &mut *self.rx_descs.add(i);
                desc.addr = self.rx_buffers[i] as u32;
                desc.buf_len = (-1536i16) as u16;
                desc.flags = 0x8000; // OWN bit
                desc.msg_len = 0;
            }

            // Setup TX descriptors
            for i in 0..NUM_TX_DESC {
                let desc = &mut *self.tx_descs.add(i);
                desc.addr = self.tx_buffers[i] as u32;
                desc.buf_len = 0;
                desc.flags = 0;
            }

            // Set init block address
            let init_addr = self.init_block as u32;
            self.write_csr(CSR_IADR0, (init_addr & 0xFFFF) as u16);
            self.write_csr(CSR_IADR1, ((init_addr >> 16) & 0xFFFF) as u16);

            // Initialize
            self.write_csr(CSR_STATUS, STATUS_INIT);

            // Wait for initialization
            for _ in 0..1000 {
                if (self.read_csr(CSR_STATUS) & STATUS_INIT) != 0 {
                    break;
                }
                core::hint::spin_loop();
            }

            // Start the device
            self.write_csr(CSR_STATUS, STATUS_STRT | STATUS_INEA);

            serial_write(b"[PCNET] Device initialized successfully\n\0".as_ptr());
        }
    }

    pub fn get_mac_address(&self) -> [u8; 6] {
        self.mac_address
    }

    pub fn transmit(&mut self, data: &[u8]) -> Result<(), &'static str> {
        if data.len() > 1536 {
            return Err("Packet too large");
        }

        unsafe {
            let desc = &mut *self.tx_descs.add(self.tx_current);

            // Wait for descriptor to be free
            while (desc.flags & 0x8000) != 0 {
                core::hint::spin_loop();
            }

            // Copy data to buffer
            core::ptr::copy_nonoverlapping(data.as_ptr(), self.tx_buffers[self.tx_current], data.len());

            // Setup descriptor
            desc.buf_len = (-(data.len() as i16)) as u16;
            desc.flags = 0x8300; // OWN | STP | ENP

            // Trigger transmission
            self.write_csr(CSR_STATUS, self.read_csr(CSR_STATUS) | STATUS_TDMD);

            self.tx_current = (self.tx_current + 1) % NUM_TX_DESC;
        }

        Ok(())
    }

    pub fn receive(&mut self) -> Option<Vec<u8>> {
        unsafe {
            let desc = &mut *self.rx_descs.add(self.rx_current);

            if (desc.flags & 0x8000) != 0 {
                return None;
            }

            if (desc.flags & 0x4000) == 0 {
                // Not start of packet
                desc.flags = 0x8000;
                self.rx_current = (self.rx_current + 1) % NUM_RX_DESC;
                return None;
            }

            let length = (desc.msg_len - 4) as usize; // Subtract CRC
            let mut packet = Vec::with_capacity(length);
            let data = core::slice::from_raw_parts(self.rx_buffers[self.rx_current], length);
            packet.extend_from_slice(data);

            // Reset descriptor
            desc.flags = 0x8000;
            desc.msg_len = 0;

            self.rx_current = (self.rx_current + 1) % NUM_RX_DESC;

            Some(packet)
        }
    }
}

impl Drop for PcnetDevice {
    fn drop(&mut self) {
        unsafe {
            for buf in &self.rx_buffers {
                rust_kfree(*buf);
            }
            for buf in &self.tx_buffers {
                rust_kfree(*buf);
            }
            rust_kfree(self.init_block as *mut u8);
            rust_kfree(self.rx_descs as *mut u8);
            rust_kfree(self.tx_descs as *mut u8);
        }
    }
}

static mut PCNET_DEVICE: Option<PcnetDevice> = None;

#[no_mangle]
pub extern "C" fn pcnet_init(io_base: u16) -> i32 {
    unsafe {
        match PcnetDevice::new(io_base) {
            Some(device) => {
                PCNET_DEVICE = Some(device);
                0
            }
            None => -1,
        }
    }
}

#[no_mangle]
pub extern "C" fn pcnet_get_mac(mac_out: *mut u8) {
    unsafe {
        if let Some(ref device) = PCNET_DEVICE {
            let mac = device.get_mac_address();
            core::ptr::copy_nonoverlapping(mac.as_ptr(), mac_out, 6);
        }
    }
}

#[no_mangle]
pub extern "C" fn pcnet_transmit(data: *const u8, len: usize) -> i32 {
    unsafe {
        if let Some(ref mut device) = PCNET_DEVICE {
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
pub extern "C" fn pcnet_receive(buffer: *mut u8, max_len: usize) -> isize {
    unsafe {
        if let Some(ref mut device) = PCNET_DEVICE {
            if let Some(packet) = device.receive() {
                let copy_len = core::cmp::min(packet.len(), max_len);
                core::ptr::copy_nonoverlapping(packet.as_ptr(), buffer, copy_len);
                return copy_len as isize;
            }
        }
        -1
    }
}
