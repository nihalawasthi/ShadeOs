#include "rtl8139.h"
#include "kernel.h"
#include "vga.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

#define RTL8139_IO_PORT    0xC000 // Default QEMU port, will scan PCI in future
#define RTL_REG_IDR0       0x00
#define RTL_REG_CMD        0x37
#define RTL_REG_RBSTART    0x30
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

static uint8_t rx_buffer[8192+16+1500];
static int initialized = 0;
static struct mac_addr mac;

static void io_wait() { for (volatile int i = 0; i < 1000; i++); }

void rtl8139_init() {
    vga_print("[NET] Initializing RTL8139...\n");
    // Reset
    outb(RTL8139_IO_PORT + RTL_REG_CMD, RTL_CMD_RESET);
    while (inb(RTL8139_IO_PORT + RTL_REG_CMD) & RTL_CMD_RESET);
    // Set up RX buffer
    outl(RTL8139_IO_PORT + RTL_REG_RBSTART, (uint32_t)(uint64_t)rx_buffer);
    // Enable RX and TX
    outb(RTL8139_IO_PORT + RTL_REG_CMD, RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);
    // Set RX config (accept all)
    outl(RTL8139_IO_PORT + RTL_REG_RCR, 0xf | (1<<7));
    // Read MAC address
    for (int i = 0; i < 6; i++) mac.addr[i] = inb(RTL8139_IO_PORT + RTL_REG_IDR0 + i);
    vga_print("[NET] MAC: ");
    for (int i = 0; i < 6; i++) {
        char hex[3] = {"0123456789ABCDEF"[(mac.addr[i]>>4)&0xF], "0123456789ABCDEF"[mac.addr[i]&0xF], 0};
        vga_print(hex);
        if (i < 5) vga_print(":");
    }
    vga_print("\n");
    initialized = 1;
}

int rtl8139_send(const void* data, int len) {
    if (!initialized) return -1;
    // Only one TX buffer for demo
    uint32_t tx_addr = RTL8139_IO_PORT + 0x20;
    for (int i = 0; i < len; i++) outb(tx_addr + i, ((const uint8_t*)data)[i]);
    outl(RTL8139_IO_PORT + 0x10, (uint32_t)(uint64_t)tx_addr);
    outl(RTL8139_IO_PORT + 0x10 + 4, len);
    return 0;
}

int rtl8139_poll_recv(void* buf, int maxlen) {
    if (!initialized) return -1;
    // Poll for received packet (not robust, demo only)
    uint16_t isr = inw(RTL8139_IO_PORT + RTL_REG_ISR);
    if (!(isr & 0x01)) return 0; // No packet
    outw(RTL8139_IO_PORT + RTL_REG_ISR, 0x01); // Ack
    int len = ((uint16_t*)rx_buffer)[1] - 4;
    if (len > maxlen) len = maxlen;
    memcpy(buf, rx_buffer + 4, len);
    return len;
}

struct mac_addr rtl8139_get_mac() { return mac; } 