#include "pci.h"
#include "kernel.h"
#include "serial.h"
#include "device.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_CLASS_CODE     0x0B
#define PCI_SUBCLASS       0x0A
#define PCI_PROG_IF        0x09
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_BAR2           0x18
#define PCI_BAR3           0x1C
#define PCI_BAR4           0x20
#define PCI_BAR5           0x24
#define PCI_INTERRUPT_LINE 0x3C

#define PCI_COMMAND_IO     0x01
#define PCI_COMMAND_MEMORY 0x02
#define PCI_COMMAND_MASTER 0x04

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar[6];
    uint8_t irq;
    int device_id_registered;
} pci_device_t;

static pci_device_t pci_devices[32];
static int pci_device_count = 0;

void pci_test_devices(void);

static uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    return (dword >> ((offset & 2) * 8)) & 0xFFFF;
}

static void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static void pci_enable_device(pci_device_t *dev) {
    uint16_t command = pci_config_read_word(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    command |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    
    uint32_t cmd_dword = pci_config_read_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND & 0xFC);
    cmd_dword = (cmd_dword & 0xFFFF0000) | command;
    pci_config_write_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND & 0xFC, cmd_dword);
}

static void pci_probe_bars(pci_device_t *dev) {
    for (int bar = 0; bar < 6; bar++) {
        uint8_t bar_offset = PCI_BAR0 + (bar * 4);
        
        /* Read current BAR value */
        uint32_t bar_value = pci_config_read_dword(dev->bus, dev->slot, dev->func, bar_offset);
        
        if (bar_value == 0) {
            dev->bar[bar] = 0;
            continue;
        }
        
        /* Write all 1s to determine size */
        pci_config_write_dword(dev->bus, dev->slot, dev->func, bar_offset, 0xFFFFFFFF);
        pci_config_read_dword(dev->bus, dev->slot, dev->func, bar_offset);
        
        /* Restore original value */
        pci_config_write_dword(dev->bus, dev->slot, dev->func, bar_offset, bar_value);
        
        dev->bar[bar] = bar_value;
    }
}

static void pci_scan_device(uint8_t bus, uint8_t slot) {
    uint16_t vendor_id = pci_config_read_word(bus, slot, 0, PCI_VENDOR_ID);
    if (vendor_id == 0xFFFF) return; /* No device */
    
    if (pci_device_count >= 32) {
        serial_write("[PCI] Too many devices, skipping\n");
        return;
    }
    
    pci_device_t *dev = &pci_devices[pci_device_count++];
    dev->bus = bus;
    dev->slot = slot;
    dev->func = 0;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_config_read_word(bus, slot, 0, PCI_DEVICE_ID);
    dev->class_code = (pci_config_read_dword(bus, slot, 0, PCI_CLASS_CODE & 0xFC) >> 24) & 0xFF;
    dev->subclass = (pci_config_read_dword(bus, slot, 0, PCI_SUBCLASS & 0xFC) >> 16) & 0xFF;
    dev->prog_if = (pci_config_read_dword(bus, slot, 0, PCI_PROG_IF & 0xFC) >> 8) & 0xFF;
    dev->irq = (pci_config_read_dword(bus, slot, 0, PCI_INTERRUPT_LINE & 0xFC) >> 0) & 0xFF;
    dev->device_id_registered = -1; // Initialize to invalid ID
    
    /* Probe BARs to get resource information */
    pci_probe_bars(dev);
    pci_enable_device(dev);

    // Check if this is a recognized device and print a detailed log if so.
    // if (dev->vendor_id == 0x10EC && dev->device_id == 0x8139) {
    //     char log_buf[128];
    //     uint32_t io_base = dev->bar[0] & ~0x3; // I/O addresses are in BAR0 for RTL8139
    //     snprintf(log_buf, sizeof(log_buf), "[rtl8139] net0: Found at %02x:%02x.%x, I/O at 0x%x, IRQ %d\n",
    //              bus, slot, 0, io_base, dev->irq);
    //     serial_write(log_buf);
    // }
    // You could add more `else if` blocks here for other supported devices (e.g., IDE controllers, sound cards).

    __asm__ volatile("" ::: "memory");
}

void pci_init(void) {
    pci_device_count = 0;
    for (uint8_t slot = 0; slot < 8; slot++) {
        pci_scan_device(0, slot);
        for (volatile int i = 0; i < 10000; i++);
    }

    if (pci_device_count == 0) {
        serial_write("[PCI] No devices found.\n");
    } else {
        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "[PCI] Found %d device(s).\n", pci_device_count);
        serial_write(log_buf);
    }
        
    // Memory barrier before returning
    __asm__ volatile("mfence" ::: "memory");
    
}

/* This function was added to provide controlled access to a device's BARs. */
/* Its declaration should be in pci.h */
uint32_t pci_get_bar(pci_device_t *dev, int bar_num) {
    if (!dev || bar_num < 0 || bar_num >= 6) {
        return 0;
    }
    return dev->bar[bar_num];
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code && pci_devices[i].subclass == subclass) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

void pci_test_devices(void) {
    extern void rust_vga_print(const char*);    
    if (pci_device_count == 0) {
        serial_write("[PCI TEST] No PCI devices found!\n");
        rust_vga_print("[PCI TEST] ERROR: No PCI devices discovered\n");
        return;
    }
    
    rust_vga_set_color(0x0A);
    rust_vga_print("Found ");
    // Print device count
    char count_str[16];
    int temp = pci_device_count;
    int i = 0;
    do {
        count_str[i++] = '0' + (temp % 10);
        temp /= 10;
    } while (temp > 0);
    count_str[i] = '\0';
    // Reverse the string
    for (int j = 0; j < i/2; j++) {
        char t = count_str[j];
        count_str[j] = count_str[i-1-j];
        count_str[i-1-j] = t;
    }
    rust_vga_print(count_str);
    rust_vga_print(" PCI devices:\n");
    rust_vga_set_color(0x0F);
    
    for (int i = 0; i < pci_device_count; i++) {
        pci_device_t *dev = &pci_devices[i];
        
        rust_vga_print("\t Device ");
        rust_vga_print(count_str); // Reuse for device number
        temp = i + 1;
        int idx = 0;
        do {
            count_str[idx++] = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);
        count_str[idx] = '\0';
        for (int j = 0; j < idx/2; j++) {
            char t = count_str[j];
            count_str[j] = count_str[idx-1-j];
            count_str[idx-1-j] = t;
        }
        rust_vga_print(count_str);
        rust_vga_print(": ");
        
        // Print vendor:device ID in hex
        char hex_buf[8];
        uint16_t vendor = dev->vendor_id;
        for (int h = 0; h < 4; h++) {
            int shift = (3 - h) * 4;
            int digit = (vendor >> shift) & 0xF;
            hex_buf[h] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        }
        hex_buf[4] = ':';
        uint16_t device = dev->device_id;
        for (int h = 0; h < 3; h++) {
            int shift = (2 - h) * 4;
            int digit = (device >> shift) & 0xF;
            hex_buf[5 + h] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        }
        hex_buf[7] = '\0';
        rust_vga_print(hex_buf);
        
        // Print class information
        rust_vga_print(" Class: ");
        temp = dev->class_code;
        idx = 0;
        do {
            count_str[idx++] = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);
        count_str[idx] = '\0';
        for (int j = 0; j < idx/2; j++) {
            char t = count_str[j];
            count_str[j] = count_str[idx-1-j];
            count_str[idx-1-j] = t;
        }
        rust_vga_print(count_str);
        
        if (dev->class_code == 0x02 && dev->subclass == 0x00) {
            rust_vga_print(" (Network Controller - Ethernet)\n");
        } else if (dev->class_code == 0x01) {
            rust_vga_print(" (Mass Storage Controller)\n");
        } else if (dev->class_code == 0x03) {
            rust_vga_print(" (Display Controller)\n");
        } else if (dev->class_code == 0x06) {
            rust_vga_print(" (Bridge Device)\n");
        } else {
            rust_vga_print(" (Other)\n");
        }
    }
    
    serial_write("[PCI TEST] PCI enumeration: SUCCESS\n");
}
