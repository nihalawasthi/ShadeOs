#include "pci.h"
#include "kernel.h"
#include "serial.h"
#include "device.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI Configuration Space Offsets
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_REVISION_ID    0x08
#define PCI_PROG_IF        0x09
#define PCI_SUBCLASS       0x0A
#define PCI_CLASS_CODE     0x0B
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10
#define PCI_INTERRUPT_LINE 0x3C

// PCI Header Types
#define PCI_HEADER_TYPE_NORMAL 0x00
#define PCI_HEADER_TYPE_BRIDGE 0x01

// PCI Command Register Bits
#define PCI_COMMAND_IO     0x01
#define PCI_COMMAND_MEMORY 0x02
#define PCI_COMMAND_MASTER 0x04

static pci_device_t pci_devices[64];
static int pci_device_count = 0;

// Forward declarations
static void pci_scan_bus(uint8_t bus);

static uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    return (dword >> ((offset & 2) * 8)) & 0xFFFF;
}

static uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    return (dword >> ((offset & 3) * 8)) & 0xFF;
}

static void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_enable_device(pci_device_t *dev) {
    uint16_t command = pci_config_read_word(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    command |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND, command);
}

static void pci_scan_func(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor_id = pci_config_read_word(bus, slot, func, PCI_VENDOR_ID);
    if (vendor_id == 0xFFFF) {
        return; // No device
    }

    if (pci_device_count >= 64) {
        serial_write("[PCI] Too many devices, skipping rest of scan.\n");
        return;
    }

    pci_device_t *dev = &pci_devices[pci_device_count++];
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_config_read_word(bus, slot, func, PCI_DEVICE_ID);
    dev->class_code = pci_config_read_byte(bus, slot, func, PCI_CLASS_CODE);
    dev->subclass = pci_config_read_byte(bus, slot, func, PCI_SUBCLASS);
    dev->prog_if = pci_config_read_byte(bus, slot, func, PCI_PROG_IF);
    dev->irq = pci_config_read_byte(bus, slot, func, PCI_INTERRUPT_LINE);
    dev->device_id_registered = -1;

    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "[PCI] Found device %02x:%02x.%x - %04x:%04x (Class %02x:%02x)\n",
             bus, slot, func, dev->vendor_id, dev->device_id, dev->class_code, dev->subclass);
    serial_write(log_buf);

    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_config_read_dword(bus, slot, func, PCI_BAR0 + (i * 4));
    }

    // If it's a PCI-to-PCI bridge, scan the bus behind it
    if (dev->class_code == 0x06 && dev->subclass == 0x04) {
        uint8_t secondary_bus = pci_config_read_byte(bus, slot, func, 0x19);
        if (secondary_bus != 0) {
            serial_write("[PCI] Scanning secondary bus: ");
            serial_write_dec("", secondary_bus);
            serial_write("\n");
            pci_scan_bus(secondary_bus);
        }
    }
}

static void pci_scan_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint16_t vendor_id = pci_config_read_word(bus, slot, 0, PCI_VENDOR_ID);
        if (vendor_id == 0xFFFF) {
            continue;
        }

        pci_scan_func(bus, slot, 0);

        uint8_t header_type = pci_config_read_byte(bus, slot, 0, PCI_HEADER_TYPE);
        if ((header_type & 0x80) != 0) { // Multi-function device
            for (uint8_t func = 1; func < 8; func++) {
                pci_scan_func(bus, slot, func);
            }
        }
    }
}

void pci_init(void) {
    pci_device_count = 0;
    serial_write("[PCI] Starting PCI bus scan...\n");
    pci_scan_bus(0); // Start scan on bus 0
    serial_write("[PCI] PCI bus scan complete.\n");

    if (pci_device_count == 0) {
        serial_write("[PCI] No devices found.\n");
    } else {
        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "[PCI] Found %d device(s).\n", pci_device_count);
        serial_write(log_buf);
    }
}

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