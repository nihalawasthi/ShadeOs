#include "rtl8139.h"
#include "kernel.h"
#include "vga.h"
#include "netdev.h"
#include "string.h"
#include "pci.h"

#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

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
static uint32_t io_base = 0;

extern void net_input_eth_frame(const uint8_t *frame, int len);
static void io_wait() { for (volatile int i = 0; i < 1000; i++); }

static int rtl8139_send_frame(net_device_t *dev, const void *frame, int len) {
    (void)dev;
    if (!initialized || !io_base) return -1;
    
    /* Only one TX buffer for demo */
    uint32_t tx_addr = io_base + 0x20;
    const uint8_t *p = (const uint8_t*)frame;
    for (int i = 0; i < len; i++) outb(tx_addr + i, p[i]);
    outl(io_base + 0x10, (uint32_t)(uint64_t)tx_addr);
    outl(io_base + 0x10 + 4, len);
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
    
    /* Poll for received packet (not robust, demo only) */
    uint16_t isr = inw(io_base + RTL_REG_ISR);
    if (!(isr & 0x01)) return 0; /* No packet */
    
    outw(io_base + RTL_REG_ISR, 0x01); /* Ack */
    int len_val = ((uint16_t*)rx_buffer)[1] - 4;
    if (len_val < 0) return 0;
    if (len_val > maxlen) len_val = maxlen;
    
    memcpy(buf, rx_buffer + 4, len_val);
    /* Also feed network layer */
    net_input_eth_frame((const uint8_t*)buf, len_val);
    return len_val;
}

struct mac_addr rtl8139_get_mac() { return mac; }
