#include "kernel.h"
#include "pci.h"
#include "serial.h"

// PCI Vendor IDs
#define VENDOR_REALTEK  0x10EC
#define VENDOR_INTEL    0x8086
#define VENDOR_AMD      0x1022

// PCI Device IDs
#define DEVICE_RTL8139         0x8139
#define DEVICE_E1000_82540EM   0x100E
#define DEVICE_E1000_82545EM   0x100F
#define DEVICE_E1000_82574L    0x10D3
#define DEVICE_PCNET_FAST3     0x2000
#define DEVICE_PCNET_HOME      0x2001

// Network device class
#define PCI_CLASS_NETWORK      0x02
#define PCI_SUBCLASS_ETHERNET  0x00

// External Rust functions
extern int pci_net_init_device(uint16_t vendor_id, uint16_t device_id, uint16_t io_base, uint64_t mem_base);
extern int pci_net_identify_device(uint16_t vendor_id, uint16_t device_id);
extern void pci_net_get_device_name(uint16_t vendor_id, uint16_t device_id, uint8_t* name_out, size_t max_len);

// PCI device enable
extern void pci_enable_device(pci_device_t *dev);

int network_init_from_pci(void) {
    serial_write("[NETINIT] Searching for network devices...\n");
    
    // Try to find an Ethernet controller
    pci_device_t *net_dev = pci_find_class(PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET);
    
    if (!net_dev) {
        serial_write("[NETINIT] No network device found on PCI bus\n");
        return -1;
    }
    
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "[NETINIT] Found network device: Vendor=%04x Device=%04x\n",
             net_dev->vendor_id, net_dev->device_id);
    serial_write(log_buf);
    
    // Get device name
    char device_name[64];
    pci_net_get_device_name(net_dev->vendor_id, net_dev->device_id, (uint8_t*)device_name, sizeof(device_name));
    snprintf(log_buf, sizeof(log_buf), "[NETINIT] Device: %s\n", device_name);
    serial_write(log_buf);
    
    // Enable the device
    pci_enable_device(net_dev);
    
    // Get IO base and memory base
    uint32_t bar0 = pci_get_bar(net_dev, 0);
    uint32_t bar1 = pci_get_bar(net_dev, 1);
    
    uint16_t io_base = 0;
    uint64_t mem_base = 0;
    
    // Check if BAR0 is IO or Memory
    if (bar0 & 0x1) {
        // IO space
        io_base = bar0 & 0xFFFC;
        snprintf(log_buf, sizeof(log_buf), "[NETINIT] IO Base: 0x%04x\n", io_base);
        serial_write(log_buf);
    } else {
        // Memory space
        mem_base = bar0 & 0xFFFFFFF0;
        snprintf(log_buf, sizeof(log_buf), "[NETINIT] Memory Base: 0x%lx\n", mem_base);
        serial_write(log_buf);
    }
    
    // If BAR1 exists and is memory, use it for E1000
    if (bar1 && !(bar1 & 0x1)) {
        mem_base = bar1 & 0xFFFFFFF0;
        snprintf(log_buf, sizeof(log_buf), "[NETINIT] Memory Base (BAR1): 0x%lx\n", mem_base);
        serial_write(log_buf);
    }
    
    // Initialize the device through Rust
    int result = pci_net_init_device(net_dev->vendor_id, net_dev->device_id, io_base, mem_base);
    
    if (result == 0) {
        serial_write("[NETINIT] Network device initialized successfully\n");
    } else {
        serial_write("[NETINIT] Failed to initialize network device\n");
    }
    
    return result;
}

// Test function to list all network devices
void network_list_devices(void) {
    serial_write("[NETINIT] Listing all network devices:\n");
    
    // Manually check for known devices
    pci_device_t *rtl8139 = pci_find_device(VENDOR_REALTEK, DEVICE_RTL8139);
    if (rtl8139) {
        serial_write("[NETINIT] - RTL8139 found\n");
    }
    
    pci_device_t *e1000_1 = pci_find_device(VENDOR_INTEL, DEVICE_E1000_82540EM);
    if (e1000_1) {
        serial_write("[NETINIT] - Intel E1000 (82540EM) found\n");
    }
    
    pci_device_t *e1000_2 = pci_find_device(VENDOR_INTEL, DEVICE_E1000_82545EM);
    if (e1000_2) {
        serial_write("[NETINIT] - Intel E1000 (82545EM) found\n");
    }
    
    pci_device_t *e1000_3 = pci_find_device(VENDOR_INTEL, DEVICE_E1000_82574L);
    if (e1000_3) {
        serial_write("[NETINIT] - Intel E1000 (82574L) found\n");
    }
    
    pci_device_t *pcnet_1 = pci_find_device(VENDOR_AMD, DEVICE_PCNET_FAST3);
    if (pcnet_1) {
        serial_write("[NETINIT] - AMD PCnet-FAST III found\n");
    }
    
    pci_device_t *pcnet_2 = pci_find_device(VENDOR_AMD, DEVICE_PCNET_HOME);
    if (pcnet_2) {
        serial_write("[NETINIT] - AMD PCnet-Home found\n");
    }
}
