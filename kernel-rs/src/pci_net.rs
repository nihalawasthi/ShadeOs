// PCI Network Device Detection and Initialization
#![allow(dead_code)]

extern "C" {
    fn serial_write(s: *const u8);
}

// PCI Vendor IDs
const VENDOR_REALTEK: u16 = 0x10EC;
const VENDOR_INTEL: u16 = 0x8086;
const VENDOR_AMD: u16 = 0x1022;

// PCI Device IDs
const DEVICE_RTL8139: u16 = 0x8139;
const DEVICE_E1000: u16 = 0x100E;
const DEVICE_E1000_82540EM: u16 = 0x100E;
const DEVICE_E1000_82545EM: u16 = 0x100F;
const DEVICE_E1000_82574L: u16 = 0x10D3;
const DEVICE_PCNET_FAST3: u16 = 0x2000;
const DEVICE_PCNET_HOME: u16 = 0x2001;

#[derive(Clone, Copy, PartialEq)]
pub enum NetworkDeviceType {
    Rtl8139,
    E1000,
    Pcnet,
    Unknown,
}

pub struct PciNetDevice {
    pub device_type: NetworkDeviceType,
    pub vendor_id: u16,
    pub device_id: u16,
    pub io_base: u16,
    pub mem_base: u64,
}

impl PciNetDevice {
    pub fn identify(vendor_id: u16, device_id: u16) -> NetworkDeviceType {
        match (vendor_id, device_id) {
            (VENDOR_REALTEK, DEVICE_RTL8139) => NetworkDeviceType::Rtl8139,
            (VENDOR_INTEL, DEVICE_E1000) |
            (VENDOR_INTEL, DEVICE_E1000_82540EM) |
            (VENDOR_INTEL, DEVICE_E1000_82545EM) |
            (VENDOR_INTEL, DEVICE_E1000_82574L) => NetworkDeviceType::E1000,
            (VENDOR_AMD, DEVICE_PCNET_FAST3) |
            (VENDOR_AMD, DEVICE_PCNET_HOME) => NetworkDeviceType::Pcnet,
            _ => NetworkDeviceType::Unknown,
        }
    }

    pub fn device_name(&self) -> &'static str {
        match self.device_type {
            NetworkDeviceType::Rtl8139 => "RTL8139",
            NetworkDeviceType::E1000 => "Intel E1000",
            NetworkDeviceType::Pcnet => "AMD PCnet",
            NetworkDeviceType::Unknown => "Unknown",
        }
    }
}

// FFI functions for C integration
#[no_mangle]
pub extern "C" fn pci_net_identify_device(vendor_id: u16, device_id: u16) -> i32 {
    match PciNetDevice::identify(vendor_id, device_id) {
        NetworkDeviceType::Rtl8139 => 1,
        NetworkDeviceType::E1000 => 2,
        NetworkDeviceType::Pcnet => 3,
        NetworkDeviceType::Unknown => 0,
    }
}

#[no_mangle]
pub extern "C" fn pci_net_init_device(vendor_id: u16, device_id: u16, io_base: u16, mem_base: u64) -> i32 {
    let device_type = PciNetDevice::identify(vendor_id, device_id);
    
    unsafe {
        match device_type {
            NetworkDeviceType::Rtl8139 => {
                serial_write(b"[PCI] Found RTL8139 network card, initializing...\n\0".as_ptr());
                extern "C" { fn network_init_rtl8139(io_base: u16) -> i32; }
                network_init_rtl8139(io_base)
            }
            NetworkDeviceType::E1000 => {
                serial_write(b"[PCI] Found Intel E1000 network card, initializing...\n\0".as_ptr());
                extern "C" { fn network_init_e1000(mem_base: u64) -> i32; }
                network_init_e1000(mem_base)
            }
            NetworkDeviceType::Pcnet => {
                serial_write(b"[PCI] Found AMD PCnet network card, initializing...\n\0".as_ptr());
                extern "C" { fn network_init_pcnet(io_base: u16) -> i32; }
                network_init_pcnet(io_base)
            }
            NetworkDeviceType::Unknown => {
                serial_write(b"[PCI] Unknown network device\n\0".as_ptr());
                -1
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn pci_net_get_device_name(vendor_id: u16, device_id: u16, name_out: *mut u8, max_len: usize) {
    let device_type = PciNetDevice::identify(vendor_id, device_id);
    let name = match device_type {
        NetworkDeviceType::Rtl8139 => b"Realtek RTL8139\0",
        NetworkDeviceType::E1000 => b"Intel E1000\0\0\0\0\0",
        NetworkDeviceType::Pcnet => b"AMD PCnet\0\0\0\0\0\0\0",
        NetworkDeviceType::Unknown => b"Unknown NIC\0\0\0\0\0",
    };
    
    let copy_len = core::cmp::min(name.len(), max_len);
    unsafe {
        core::ptr::copy_nonoverlapping(name.as_ptr(), name_out, copy_len);
    }
}
