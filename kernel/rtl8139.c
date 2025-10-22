#include <stdint.h>

// Simple identity-mapping stub for virt_to_phys (replace with real mapping if needed)
static inline uint32_t virt_to_phys(const void *addr) {
    return (uint32_t)(uintptr_t)addr;
}
#include "rtl8139.h"
#include "kernel.h"
#include "vga.h"
#include "netdev.h"
#include "string.h"
#include "serial.h"
#include "pci.h"

#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

#define RTL_REG_IDR0       0x00
#define RTL_REG_CMD        0x37
#define RTL_REG_RBSTART    0x30
#define RTL_REG_TSD0       0x10
#define RTL_REG_TSD1       0x14
#define RTL_REG_TSD2       0x18
#define RTL_REG_TSD3       0x1C
#define RTL_REG_TSAD0      0x20
#define RTL_REG_TSAD1      0x24
#define RTL_REG_TSAD2      0x28
#define RTL_REG_TSAD3      0x2C
#define RTL_REG_IMR        0x3C
#define RTL_REG_RCR        0x44
#define RTL_REG_TCR        0x40
#define RTL_REG_CAPR       0x38
#define RTL_REG_CBR        0x3A
#define RTL_REG_ISR        0x3E
#define RTL_REG_MSR        0x58
#define RTL_CMD_RX_ENABLE  0x08
#define RTL_CMD_TX_ENABLE  0x04
#define RTL_CMD_RESET      0x10
#define TSD_OWN (1 << 13)

static uint8_t rx_buffer[8192+16+1500];
static uint8_t tx_buffers[4][2048];
static int tx_current = 0;
static uint16_t rx_read_ptr = 0;
static int initialized = 0;
static struct mac_addr mac;
static uint32_t io_base = 0;

extern void net_input_eth_frame(const uint8_t *frame, int len);

static int rtl8139_send_frame(net_device_t *dev, const void *frame, int len) {
    (void)dev;
    serial_write("[RTL8139] rtl8139_send_frame: entered\n");
    // Print all TX descriptor statuses
    for (int i = 0; i < 4; ++i) {
        uint32_t tsd_reg = RTL_REG_TSD0 + (i * 4);
        uint32_t status = inl(io_base + tsd_reg);
        serial_write("[RTL8139] TX descriptor ");
        serial_write_dec("", i);
    serial_write(": status=0x");
    serial_write_hex("", status);
    serial_write(" (OWN=");
    serial_write_dec("", ((status & TSD_OWN) == TSD_OWN) ? 1 : 0);
    serial_write(") ");
    serial_write("[TSD_OWN=0x");
    serial_write_hex("", TSD_OWN);
    serial_write("] ");
    serial_write("[status&TSD_OWN=0x");
    serial_write_hex("", (status & TSD_OWN));
    serial_write("]\n");
    }
    if (!initialized || !io_base) {
        serial_write("[RTL8139] Not initialized or no io_base\n");
        return -1;
    }
    if (len > 2048) {
        serial_write("[RTL8139] Frame too large\n");
        return -1;
    }

    // Get the registers for the current TX descriptor
    uint32_t tsd_reg = RTL_REG_TSD0 + (tx_current * 4);
    uint32_t tsad_reg = RTL_REG_TSAD0 + (tx_current * 4);

    // Check if the descriptor is free (OWN bit is 0)
    uint32_t status = inl(io_base + tsd_reg);
    // Only treat as busy if OWN bit (0x2000) is set, not if TOK (0x200) is set
    if ((status & TSD_OWN) != 0) {
        serial_write("[RTL8139] TX descriptor OWN bit set (0x2000), busy, dropping packet\n");
        serial_write("[RTL8139] Descriptor status=0x");
        serial_write_hex("", status);
        serial_write("\n");
        return -1;
    }

    serial_write("[RTL8139] Copying frame to DMA buffer\n");
    memcpy(tx_buffers[tx_current], frame, len);

    serial_write("[RTL8139] Writing buffer address to TSAD\n");
    uint32_t phys_addr = virt_to_phys(tx_buffers[tx_current]);
    serial_write("[RTL8139] TX buffer phys addr: 0x");
    serial_write_hex("", phys_addr);
    serial_write("\n");
    outl(io_base + tsad_reg, phys_addr);

    serial_write("[RTL8139] Writing length to TSD to start transmission\n");
    uint32_t tsd_val = (uint32_t)len;
    // Set EOR (End Of Ring) bit if this is the last descriptor
    if (tx_current == 3) tsd_val |= (1 << 30); // EOR is bit 30
    serial_write("[RTL8139] TSD value: 0x");
    serial_write_hex("", tsd_val);
    serial_write("\n");
    outl(io_base + tsd_reg, tsd_val);

    // Move to the next descriptor for the next send.
    tx_current = (tx_current + 1) % 4;

    serial_write("[RTL8139] Frame sent\n");
    return 0;
}

void rtl8139_init() {
    vga_print("[NET] Initializing RTL8139...\n");
    
    /* Find RTL8139 via PCI enumeration */
    pci_device_t *pci_dev = pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    if (!pci_dev) {
        vga_print("[NET] RTL8139 not found via PCI\n");
        return;
    }
    
    /* This function should be declared in pci.h */
    uint32_t pci_get_bar(pci_device_t *dev, int bar_num);

    /* Get I/O base from BAR0 */
    io_base = pci_get_bar(pci_dev, 0) & 0xFFFFFFFC;
    if (!io_base) {
        vga_print("[NET] RTL8139 has no I/O BAR\n");
        return;
    }
    
    vga_print("[NET] RTL8139 found at I/O base 0x");
    char hex_str[9];
    for (int i = 0; i < 8; i++) {
        int nibble = (io_base >> ((7-i) * 4)) & 0xF;
        hex_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    hex_str[8] = 0;
    vga_print(hex_str);
    vga_print("\n");
    
    /* Reset */
    outb(io_base + RTL_REG_CMD, RTL_CMD_RESET);
    while (inb(io_base + RTL_REG_CMD) & RTL_CMD_RESET);
    
    /* Set up RX buffer */
    outl(io_base + RTL_REG_RBSTART, (uint32_t)(uint64_t)rx_buffer);
    
    /* Enable RX and TX */
    outb(io_base + RTL_REG_CMD, RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);
    
    /* Set RX config (accept all) */
    outl(io_base + RTL_REG_RCR, 0xf | (1<<7));
    
    /* Read MAC address */
    for (int i = 0; i < 6; i++) mac.addr[i] = inb(io_base + RTL_REG_IDR0 + i);
    
    vga_print("[NET] MAC: ");
    for (int i = 0; i < 6; i++) {
        char hex[3] = {"0123456789ABCDEF"[(mac.addr[i]>>4)&0xF], "0123456789ABCDEF"[mac.addr[i]&0xF], 0};
        vga_print(hex);
        if (i < 5) vga_print(":");
    }
    vga_print("\n");
    
    initialized = 1;

    /* Register as net device */
    netdev_register("rtl8139", mac.addr, 1500, rtl8139_send_frame, (void*)pci_dev);
    vga_print("[NET] RTL8139 initialization complete\n");
}

int rtl8139_send(const void* data, int len) {
    if (!initialized || !io_base) return -1;
    /* Back-compat direct send */
    return rtl8139_send_frame(0, data, len);
}

int rtl8139_poll_recv(void* buf, int maxlen) {
    if (!initialized || !io_base) return -1;
    
    // Check if the buffer is empty by checking the CMD register's BUFE bit
    if (inb(io_base + RTL_REG_CMD) & 1) {
        return 0;
    }

    // Pointer to the current location in our rx_buffer
    uint8_t* rx_ptr = rx_buffer + rx_read_ptr;

    // The NIC prepends a 4-byte header to each packet
    uint16_t pkt_status = *(uint16_t*)rx_ptr;
    uint16_t pkt_len = *(uint16_t*)(rx_ptr + 2);

    // Check for Receive OK status
    if (pkt_status & 1) {
        // Actual packet data starts after the 4-byte header
        uint8_t* pkt_data = rx_ptr + 4;
        int frame_len = pkt_len - 4; // Subtract CRC

        if (frame_len > maxlen) frame_len = maxlen;
        if (frame_len >= 14) { // Valid ethernet frame
            memcpy(buf, pkt_data, frame_len);
            // Advance the read pointer
            uint16_t new_read_ptr = (rx_read_ptr + pkt_len + 4 + 3) & ~3;
            rx_read_ptr = new_read_ptr % (sizeof(rx_buffer) - 1500); // Wrap around
            outw(io_base + RTL_REG_CAPR, rx_read_ptr - 16);
            return frame_len;
        }
    }
    // On error or small packet, just advance past it
    uint16_t new_read_ptr = (rx_read_ptr + pkt_len + 4 + 3) & ~3;
    rx_read_ptr = new_read_ptr % (sizeof(rx_buffer) - 1500);
    outw(io_base + RTL_REG_CAPR, rx_read_ptr - 16);
    return 0;
}

struct mac_addr rtl8139_get_mac() { return mac; }
