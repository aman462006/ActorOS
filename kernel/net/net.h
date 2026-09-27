#pragma once
#include "../include/actor.h"

/* Network message types */
#define MSG_NET_TX          50   /* transmit a raw frame: data[0..3]=len, data[4..47]=frame */
#define MSG_NET_RX          51   /* received frame: data[0..3]=len, data[4..47]=frame */
#define MSG_NET_LINK_UP     52   /* link state change */
#define MSG_NET_LINK_DOWN   53

#define MSG_ETH_TX          54   /* Ethernet: data[0..5]=dst, data[6..9]=ethertype, data[10..47]=payload */
#define MSG_ETH_RX          55

#define MSG_IP_TX           56   /* IP: data[0..3]=dst_ip, data[4]=proto, data[5..43]=payload */
#define MSG_IP_RX           57

#define MSG_TCP_CONNECT     58
#define MSG_TCP_DATA        59
#define MSG_TCP_CLOSE       60

/* Well-known actor IDs for network stack */
extern uint64_t net_e1000_id;   /* E1000 NIC driver actor */
extern uint64_t net_eth_id;     /* Ethernet layer actor */
extern uint64_t net_ip_id;      /* IP layer actor */

/* Spawn the network stack.  Does nothing if no E1000 is found. */
void net_init(Actor* parent);
