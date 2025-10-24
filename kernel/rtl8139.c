#include <stdint.h>
#include "serial.h"
#include "paging.h"

/* Translate kernel virtual address to physical. Prefer the page-table walk
   helper `get_phys_addr()`; fall back to identity cast if it returns 0. */
static inline uint32_t virt_to_phys(const void *addr) {
    uint64_t phys = get_phys_addr((uint64_t)addr);
    if (!phys) {
        serial_write("[RTL8139] WARNING: get_phys_addr returned 0, using fallback cast\n");
        phys = (uint64_t)(uintptr_t)addr;
    }
    return (uint32_t)phys;
}
#include "rtl8139.h"
#include "kernel.h"
#include "vga.h"
#include "netdev.h"
#include "string.h"
#include "serial.h"
#include "pci.h"
#include "idt.h"
#include "timer.h"

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

/* Forward declarations for IRQ handler and poller implemented below */
void rtl8139_irq_handler(registers_t regs);
void rtl8139_tx_poll(void);

static int rtl8139_send_frame(net_device_t *dev, const void *frame, int len) {
    (void)dev;

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

    // Check if the descriptor is free (OWN bit is 0)
        uint32_t status = 0;
        int attempts = 0;
        while (attempts < 5) {
            status = inl(io_base + tsd_reg);
            if ((status & TSD_OWN) == 0) break;
            /* small busy-wait */
            for (volatile int z = 0; z < 1000; ++z) __asm__ volatile ("nop");
            attempts++;
        }
        if ((status & TSD_OWN) != 0) {
            serial_write("[RTL8139] TX descriptor OWN bit set after retries, dropping packet\n");
            return -1;
        }
    memcpy(tx_buffers[tx_current], frame, len);
     /* TSAD registers were written during initialization to point to the
         physical addresses of tx_buffers[]. Only write TSD here (length + EOR)
         to hand the descriptor to the NIC. */
     uint32_t tsd_val = (uint32_t)(len & 0x1FFF); /* len in low 13 bits */
     if (tx_current == 3) tsd_val |= (1u << 30); /* EOR bit */
    outl(io_base + tsd_reg, tsd_val);

     // Move to the next descriptor for the next send.
     tx_current = (tx_current + 1) % 4;
     return len;
}

void rtl8139_init() {
    /* Enable RX and TX interrupts in IMR */
    outw(io_base + RTL_REG_IMR, 0x0005); // RXOK (bit 0) | TXOK (bit 2)
    uint16_t imr = inw(io_base + RTL_REG_IMR);
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
    
    /* Set up RX buffer (write physical address) */
    uint32_t rb_phys = virt_to_phys(rx_buffer);
    outl(io_base + RTL_REG_RBSTART, rb_phys);

    /* Initialize TX descriptor TSAD/TSD entries with physical addresses and
       clear TSD so NIC sees clean descriptors. */
    for (int i = 0; i < 4; ++i) {
        uint32_t tsad_reg = RTL_REG_TSAD0 + (i * 4);
        uint32_t tsd_reg = RTL_REG_TSD0 + (i * 4);
        uint32_t tx_phys = virt_to_phys(tx_buffers[i]);
        outl(io_base + tsad_reg, tx_phys);
        outl(io_base + tsd_reg, 0x00000000);
    }

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
    // Unmask PIC IRQ line for this device so we can receive interrupts
    if (pci_dev) {
        uint8_t irq = pci_dev->irq;
        if (irq < 16) {
            if (irq < 8) {
                uint8_t mask = inb(0x21);
                mask &= ~(1u << irq);
                outb(0x21, mask);
            } else {
                uint8_t mask = inb(0xA1);
                mask &= ~(1u << (irq - 8));
                outb(0xA1, mask);
            }
            // Register C-level interrupt handler at vector (irq + 32)
            register_interrupt_handler(32 + irq, rtl8139_irq_handler);
        }
    }
    vga_print("[NET] RTL8139 initialization complete\n");

    // Remap DMA buffers to be device/uncached to avoid cache coherence issues.
    // Remap rx_buffer
    uint64_t rx_start = (uint64_t)rx_buffer;
    uint64_t rx_end = rx_start + sizeof(rx_buffer);
    for (uint64_t a = rx_start & ~0xFFFULL; a < rx_end; a += 0x1000) {
        uint64_t phys = get_phys_addr(a);
        if (!phys) continue;
        map_page(a, phys, PAGE_PRESENT | PAGE_RW | PAGE_DEVICE);
    }
    // Remap tx buffers
    for (int i = 0; i < 4; ++i) {
        uint64_t tx_start = (uint64_t)tx_buffers[i];
        uint64_t tx_end = tx_start + sizeof(tx_buffers[i]);
        for (uint64_t a = tx_start & ~0xFFFULL; a < tx_end; a += 0x1000) {
            uint64_t phys = get_phys_addr(a);
            if (!phys) continue;
            map_page(a, phys, PAGE_PRESENT | PAGE_RW | PAGE_DEVICE);
        }
    }

    // Register a periodic TX completion poller (runs every 500ms)
    timer_register_periodic(rtl8139_tx_poll, 500);
}

// IRQ handler for RTL8139: called with vector = irq + 32
void rtl8139_irq_handler(registers_t regs) {
    (void)regs;
    if (!io_base) return;
    uint8_t isr = inb(io_base + RTL_REG_ISR);
    serial_write("[RTL8139-IRQ] Entered IRQ handler\n");
    if (!isr) return;
    // Acknowledge/clear bits by writing back the ISR value
    outb(io_base + RTL_REG_ISR, isr);
    serial_write("[RTL8139-IRQ] ISR=0x"); serial_write_hex("", isr); serial_write("\n");
    // If RX (bit 0) or TX (bit 2) set, poll RX/TX
    if (isr & 0x01) {
        // Data received
        net_poll_rx();
    }
    if (isr & 0x04) {
        // TX OK - scan TSDs for completions
        for (int i = 0; i < 4; ++i) {
            uint32_t tsd = inl(io_base + RTL_REG_TSD0 + i*4);
            if (tsd != 0) {
                // If OWN cleared and TOK set, it's a completion
                if ((tsd & TSD_OWN) == 0) {
                    serial_write("[RTL8139-IRQ] TX desc "); serial_write_dec("", i);
                    serial_write(" completed, status=0x"); serial_write_hex("", tsd); serial_write("\n");
                }
            }
        }
    }
    // Always log TSD status for all descriptors
    for (int i = 0; i < 4; ++i) {
        uint32_t tsd = inl(io_base + RTL_REG_TSD0 + i*4);
        serial_write("[RTL8139-IRQ] TSD["); serial_write_dec("", i); serial_write("] = 0x");
        serial_write_hex("", tsd); serial_write("\n");
    }
}

// Periodic TX completion poller: check TSD registers and log completions
void rtl8139_tx_poll(void) {
    if (!initialized || !io_base) return;
    for (int i = 0; i < 4; ++i) {
        uint32_t tsd = inl(io_base + RTL_REG_TSD0 + i*4);
        // If descriptor not OWN and non-zero, it likely contains status
        if (tsd != 0 && (tsd & TSD_OWN) == 0) {
            serial_write("[RTL8139-POLL] TX desc "); serial_write_dec("", i);
            serial_write(" status=0x"); serial_write_hex("", tsd); serial_write("\n");
        }
    }
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
