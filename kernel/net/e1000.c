/* e1000.c — E1000 NIC driver actor.
 *
 * PCI scan: walks bus 0, devices 0-31, functions 0-7 looking for
 * vendor=0x8086 (Intel), device in the E1000 family (0x100E, 0x100F, 0x10D3).
 *
 * Once found: BAR0 gives MMIO base, TX/RX descriptor rings are set up,
 * and the actor sits in a receive loop, forwarding frames upward as MSG_NET_RX.
 *
 * This is a skeleton — MMIO register reads/writes and DMA are stubbed with
 * serial log messages so the actor tree boots without real hardware.
 */

#include "../include/actor.h"
#include "../include/mm.h"
#include "../include/io.h"
#include "net.h"

extern void serial_puts(const char* s);
extern void serial_putdec(uint64_t v);
extern void serial_puthex(uint64_t v);

/* ── PCI config-space access via IO ports ── */
#define PCI_CONFIG_ADDR 0x0CF8
#define PCI_CONFIG_DATA 0x0CFC

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func << 8)
                  | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

/* ── E1000 MMIO register offsets ── */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_CTRL_RST  (1u << 26)
#define E1000_CTRL_ASDE (1u << 5)
#define E1000_CTRL_SLU  (1u << 6)

typedef struct {
    uintptr_t  mmio_base;
    uint8_t    mac[6];
    bool       found;
} E1000State;

/* Known E1000 PCI device IDs */
static const uint16_t E1000_IDS[] = { 0x100E, 0x100F, 0x10D3, 0x1533, 0 };

static bool is_e1000(uint16_t devid) {
    for (int i = 0; E1000_IDS[i]; i++)
        if (E1000_IDS[i] == devid) return true;
    return false;
}

static bool e1000_scan(E1000State* e) {
    for (uint8_t bus = 0; bus < 4; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0, 0);
            uint16_t vendor = id & 0xFFFF;
            uint16_t devid  = id >> 16;
            if (vendor == 0xFFFF) continue;
            if (vendor != 0x8086) continue;
            if (!is_e1000(devid)) continue;

            serial_puts("[e1000] found Intel NIC bus=");
            serial_putdec(bus);
            serial_puts(" dev=");
            serial_putdec(dev);
            serial_puts(" devid=");
            serial_puthex(devid);
            serial_puts("\n");

            /* BAR0 = MMIO base (32-bit, bit 0 = 0 → memory BAR) */
            uint32_t bar0 = pci_read32(bus, dev, 0, 0x10) & ~0xFu;
            e->mmio_base = (uintptr_t)bar0;

            /* Enable bus master + memory space */
            uint32_t cmd = pci_read32(bus, dev, 0, 0x04);
            pci_write32(bus, dev, 0, 0x04, cmd | 0x6);

            e->found = true;
            return true;
        }
    }
    return false;
}

static void e1000_read_mac(E1000State* e) {
    /* MAC is in RAL/RAH registers at MMIO offset 0x5400/0x5404 (or EEPROM).
       For QEMU E1000, RAL/RAH are valid after reset. */
    volatile uint32_t* ral = (volatile uint32_t*)(e->mmio_base + 0x5400);
    volatile uint32_t* rah = (volatile uint32_t*)(e->mmio_base + 0x5404);
    uint32_t lo = *ral;
    uint32_t hi = *rah;
    e->mac[0] = lo & 0xFF;
    e->mac[1] = (lo >> 8) & 0xFF;
    e->mac[2] = (lo >> 16) & 0xFF;
    e->mac[3] = (lo >> 24) & 0xFF;
    e->mac[4] = hi & 0xFF;
    e->mac[5] = (hi >> 8) & 0xFF;

    serial_puts("[e1000] MAC ");
    for (int i = 0; i < 6; i++) {
        serial_puthex(e->mac[i]);
        if (i < 5) serial_puts(":");
    }
    serial_puts("\n");
}

static void e1000_reset(E1000State* e) {
    volatile uint32_t* ctrl = (volatile uint32_t*)(e->mmio_base + E1000_CTRL);
    *ctrl = E1000_CTRL_RST;
    /* Short spin instead of timer — kernel has no sleep yet */
    for (volatile int i = 0; i < 100000; i++);
    *ctrl = E1000_CTRL_ASDE | E1000_CTRL_SLU;
}

/* ── E1000 driver actor entry ── */
uint64_t net_e1000_id = 0;
uint64_t net_eth_id   = 0;
uint64_t net_ip_id    = 0;

void e1000_actor_entry(Actor* self) {
    net_e1000_id = self->id;

    E1000State* e = (E1000State*)kzalloc(sizeof(E1000State));
    if (!e) return;

    if (!e1000_scan(e)) {
        serial_puts("[e1000] no NIC found — network disabled\n");
        kfree(e);
        return;
    }

    e1000_reset(e);
    e1000_read_mac(e);

    serial_puts("[e1000] driver actor running, id=");
    serial_putdec(self->id);
    serial_puts("\n");

    /* Main loop — wait for TX requests or RX interrupts */
    Message msg;
    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_NET_TX: {
                /* Send frame to wire (stub) */
                uint32_t frame_len = msg_get_u32(&msg, 0);
                serial_puts("[e1000] TX ");
                serial_putdec(frame_len);
                serial_puts(" bytes (stub)\n");
                break;
            }
            case MSG_INTERRUPT: {
                /* NIC IRQ — check ICR and forward received frames upward */
                serial_puts("[e1000] IRQ (stub)\n");
                break;
            }
            case MSG_SHUTDOWN:
                kfree(e);
                return;
            default:
                break;
        }
    }
}
