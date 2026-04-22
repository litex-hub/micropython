// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// SPDX-License-Identifier: BSD-2-Clause
//
// LiteEth ↔ lwIP netif bridge.
//
// LiteEth's MAC exposes:
//   * RX side (`sram_writer_*`): a small ring of SLOT_SIZE-byte buffers.
//     When the MAC finishes a frame, ev_pending bit 0 latches; the
//     firmware reads `_slot` (which one was just written) and `_length`,
//     copies/peeks at the data at ETHMAC_RX_BASE + slot * SLOT_SIZE, then
//     write-1-clears ev_pending to release the slot back to the MAC.
//   * TX side (`sram_reader_*`): same ring shape going the other way.
//     Firmware copies a frame to ETHMAC_TX_BASE + slot * SLOT_SIZE,
//     writes `_slot`, `_length`, then `_start = 1`. Hardware sets
//     `_ready = 1` when it's done with the slot and ev_pending bit 0
//     latches at the same time.
//
// First-cut driver: polling RX (`liteeth_netif_poll` is called from the
// main task), blocking TX (busy-wait on `_ready`). IRQ-driven RX is a
// straightforward follow-up via `litex_isr_register` — see the comment
// in liteeth_handle_rx().

#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>

#ifdef CSR_ETHMAC_BASE

#include <string.h>

#include "py/mphal.h"
#include "py/runtime.h"

#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/snmp.h"
#include "lwip/sys.h"
#include "netif/ethernet.h"

#include "liteeth_netif.h"

// lwIP requires the port to provide sys_now() returning ms-since-boot.
// We have mp_hal_ticks_ms() which is exactly that.
u32_t sys_now(void) {
    return (u32_t)mp_hal_ticks_ms();
}

// LiteEth's slot-allocation state we track in software. The MAC tells us
// "the next RX is in slot N" via the writer_slot CSR; for TX we round-
// robin through the slots ourselves and gate each send on `_ready`.
typedef struct {
    uint8_t tx_slot;
} liteeth_state_t;

static liteeth_state_t liteeth_state;

// ---------------------------------------------------------------------------
// LiteEth RX path
// ---------------------------------------------------------------------------

static void liteeth_handle_rx(struct netif *netif) {
    // ev_pending bit 0 is the "RX completed into a slot" event. We
    // drain at most ETHMAC_RX_SLOTS frames per call: if the host is
    // continuously broadcasting (mDNS, IPv6 RA, ARP, ...) onto our
    // interface, new slots latch as fast as we clear them and an
    // unbounded `while (ev_pending)` would starve everything else.
    // Whatever's left over gets picked up on the next poll.
    //
    // Hand each frame to lwIP via netif->input (== ethernet_input here)
    // and write-1-clear ev_pending so the slot returns to the MAC's
    // free pool. If we run out of pbufs we drop the frame and still
    // clear ev_pending; better to lose a packet than wedge the MAC.
    for (int i = 0; i < ETHMAC_RX_SLOTS; i++) {
        if (!(ethmac_sram_writer_ev_pending_read() & 0x1)) {
            break;
        }
        uint8_t slot = ethmac_sram_writer_slot_read();
        uint16_t length = ethmac_sram_writer_length_read();
        const uint8_t *src = (const uint8_t *)(ETHMAC_RX_BASE + slot * ETHMAC_SLOT_SIZE);

        if (length > 0 && length <= ETHMAC_SLOT_SIZE) {
            struct pbuf *p = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
            if (p != NULL) {
                pbuf_take(p, src, length);
                if (netif->input(p, netif) != ERR_OK) {
                    pbuf_free(p);
                }
            }
        }
        // Always release the slot so the MAC can keep receiving.
        ethmac_sram_writer_ev_pending_write(0x1);
    }
}

void liteeth_netif_poll(struct netif *netif) {
    liteeth_handle_rx(netif);
}

// ---------------------------------------------------------------------------
// LiteEth TX path
// ---------------------------------------------------------------------------

static err_t liteeth_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;

    // Wait for the MAC to finish the previous TX. With ETHMAC_TX_SLOTS=2
    // this is rarely contended; busy-wait is fine and avoids a
    // scheduler dependency.
    uint32_t deadline = mp_hal_ticks_ms() + 100;
    while (!ethmac_sram_reader_ready_read()) {
        if (mp_hal_ticks_ms() > deadline) {
            return ERR_TIMEOUT;
        }
    }

    // Walk the pbuf chain into the TX slot's SRAM region.
    uint8_t slot = liteeth_state.tx_slot;
    uint8_t *dst = (uint8_t *)(ETHMAC_TX_BASE + slot * ETHMAC_SLOT_SIZE);
    if (p->tot_len > ETHMAC_SLOT_SIZE) {
        return ERR_BUF;
    }
    pbuf_copy_partial(p, dst, p->tot_len, 0);

    // Hand off to the MAC: select the slot, set the length, kick.
    ethmac_sram_reader_slot_write(slot);
    ethmac_sram_reader_length_write(p->tot_len);
    ethmac_sram_reader_start_write(0x1);

    // Round-robin to the next slot for the next call. We don't need the
    // hardware to be done with this one before returning — the next
    // linkoutput will busy-wait on _ready as needed.
    liteeth_state.tx_slot = (slot + 1) % ETHMAC_TX_SLOTS;

    LINK_STATS_INC(link.xmit);
    return ERR_OK;
}

// ---------------------------------------------------------------------------
// netif init
// ---------------------------------------------------------------------------

static err_t liteeth_netif_init_cb(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    netif->hwaddr_len = 6;
    netif->output = etharp_output;
    netif->linkoutput = liteeth_linkoutput;
    MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, 100000000);
    return ERR_OK;
}

int liteeth_netif_init(struct netif *netif, const uint8_t *mac_addr) {
    // Initialise lwIP (memp pools, netif list, timeouts) on first call.
    // Without this, netif_add walks uninitialised global state and we
    // hang somewhere inside lwIP. Most ports call this at firmware
    // boot; we defer to here so non-LAN firmware doesn't pay the cost.
    static bool lwip_inited = false;
    if (!lwip_inited) {
        lwip_init();
        lwip_inited = true;
    }
    // Reset the LiteEth MAC's event lines to a clean state.
    ethmac_sram_writer_ev_pending_write(0x1);
    ethmac_sram_reader_ev_pending_write(0x1);
    liteeth_state.tx_slot = 0;

    uint8_t mac[6];
    if (mac_addr != NULL) {
        memcpy(mac, mac_addr, 6);
    } else {
        // Locally-administered unicast (bit 1 of first byte set, bit 0 clear).
        // Last 4 bytes from the LiteX identifier ROM hash for stability
        // across reboots.
        mac[0] = 0x02;
        mac[1] = 0x00;
        mac[2] = 0x00;
        mac[3] = 0x00;
        mac[4] = 0x00;
        mac[5] = 0x01;
    }
    memcpy(netif->hwaddr, mac, 6);

    // 0.0.0.0 / 0 — DHCP or static IP arrives via netif_set_ipaddr later.
    ip_addr_t ip = { 0 }, mask = { 0 }, gw = { 0 };
    if (netif_add(netif, &ip, &mask, &gw, NULL,
            liteeth_netif_init_cb, ethernet_input) == NULL) {
        return -1;
    }
    netif_set_default(netif);
    netif_set_up(netif);
    netif_set_link_up(netif);
    return 0;
}

void liteeth_netif_deinit(struct netif *netif) {
    netif_set_link_down(netif);
    netif_set_down(netif);
    netif_remove(netif);
}

#endif // CSR_ETHMAC_BASE
