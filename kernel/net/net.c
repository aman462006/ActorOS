/* net.c — Network stack actor hierarchy.
 * Spawns: e1000 (NIC driver) → ethernet → IP
 * Each layer is an actor that receives frames from the layer below
 * and sends processed frames to the layer above.
 */

#include "../include/actor.h"
#include "../include/mm.h"
#include "net.h"

extern void serial_puts(const char* s);
extern void serial_putdec(uint64_t v);

/* ── Ethernet layer actor ── */
static void eth_actor_entry(Actor* self) {
    net_eth_id = self->id;
    serial_puts("[eth] actor id=");
    serial_putdec(self->id);
    serial_puts("\n");

    Message msg;
    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_NET_RX: {
                /* Raw frame received from NIC driver.
                   Parse Ethernet header: dst[6] src[6] ethertype[2] payload */
                uint32_t frame_len = msg_get_u32(&msg, 0);
                if (frame_len < 14) break;  /* too short */

                uint16_t ethertype = ((uint16_t)msg.data[16] << 8) | msg.data[17];
                /* 0x0800 = IPv4, 0x0806 = ARP */
                if (ethertype == 0x0800 && net_ip_id) {
                    /* Forward to IP layer */
                    Message fwd;
                    kmemset(&fwd, 0, sizeof(fwd));
                    fwd.type = MSG_ETH_RX;
                    uint32_t pay_len = frame_len - 14;
                    if (pay_len > MSG_MAX_DATA - 4) pay_len = MSG_MAX_DATA - 4;
                    msg_set_u32(&fwd, 0, pay_len);
                    __builtin_memcpy(fwd.data + 4, msg.data + 18, pay_len);
                    fwd.len = 4 + pay_len;
                    actor_send_direct(net_ip_id, &fwd);
                }
                break;
            }
            case MSG_ETH_TX: {
                /* Forward raw frame to NIC driver */
                if (net_e1000_id) actor_send_direct(net_e1000_id, &msg);
                break;
            }
            case MSG_SHUTDOWN: return;
            default: break;
        }
    }
}

/* ── IP layer actor ── */
static void ip_actor_entry(Actor* self) {
    net_ip_id = self->id;
    serial_puts("[ip] actor id=");
    serial_putdec(self->id);
    serial_puts("\n");

    Message msg;
    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_ETH_RX: {
                /* IP packet: parse header, extract protocol, stub for now */
                uint32_t len = msg_get_u32(&msg, 0);
                if (len < 20) break;  /* IP header is 20 bytes minimum */
                uint8_t proto = msg.data[13];  /* offset 4=data start + 9=proto */
                serial_puts("[ip] RX proto=");
                serial_putdec(proto);
                serial_puts(" len=");
                serial_putdec(len);
                serial_puts(" (stub)\n");
                break;
            }
            case MSG_IP_TX: {
                /* Build IP header and forward down to Ethernet layer */
                serial_puts("[ip] TX (stub)\n");
                break;
            }
            case MSG_SHUTDOWN: return;
            default: break;
        }
    }
}

/* ── Stack initialization ── */
void net_init(Actor* parent) {
    serial_puts("[net] initializing network stack\n");

    extern void e1000_actor_entry(Actor*);

    /* Spawn in order: e1000 → ethernet → IP */
    int e1000_cap = actor_spawn(parent, "e1000", e1000_actor_entry, ACTOR_PRIO_HIGH);
    if (e1000_cap < 0) {
        serial_puts("[net] failed to spawn e1000 actor\n");
        return;
    }
    Capability e1000_cap_val;
    cap_get(parent, (uint32_t)e1000_cap, &e1000_cap_val);
    net_e1000_id = e1000_cap_val.actor_id;

    int eth_cap = actor_spawn(parent, "ethernet", eth_actor_entry, ACTOR_PRIO_HIGH);
    if (eth_cap >= 0) {
        Capability eth_cap_val;
        cap_get(parent, (uint32_t)eth_cap, &eth_cap_val);
        net_eth_id = eth_cap_val.actor_id;
    }

    int ip_cap = actor_spawn(parent, "ip", ip_actor_entry, ACTOR_PRIO_NORMAL);
    if (ip_cap >= 0) {
        Capability ip_cap_val;
        cap_get(parent, (uint32_t)ip_cap, &ip_cap_val);
        net_ip_id = ip_cap_val.actor_id;
    }

    serial_puts("[net] stack actors spawned: e1000/eth/ip\n");
}
