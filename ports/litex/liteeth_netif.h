// Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
// SPDX-License-Identifier: BSD-2-Clause
//
// Glue between LiteEth's MAC (ETHMAC slot CSRs + ETHMAC_RX/TX SRAM
// regions) and lwIP. Used by network_lan.c (Python `network.LAN`).

#ifndef MICROPY_INCLUDED_LITEX_LITEETH_NETIF_H
#define MICROPY_INCLUDED_LITEX_LITEETH_NETIF_H

#include <stdint.h>

#ifdef CSR_ETHMAC_BASE

#include "lwip/netif.h"

// Initialise the LiteEth MAC and bring up an lwIP netif against it.
// Returns 0 on success, -errno on failure (mainly out-of-memory).
//
// `mac_addr` is a 6-byte array used for the netif's hwaddr. Pass NULL to
// derive a deterministic MAC from the LiteX SoC identifier (good enough
// for sim and bring-up; production firmware should pass a per-unit MAC).
int liteeth_netif_init(struct netif *netif, const uint8_t *mac_addr);

// Tear down the netif. Mostly here so network.LAN(0).active(False) is
// reversible.
void liteeth_netif_deinit(struct netif *netif);

// Drain any RX events into lwIP. Call from the main loop / scheduler tick
// — until the IRQ path is wired we drive the MAC by polling.
void liteeth_netif_poll(struct netif *netif);

#endif // CSR_ETHMAC_BASE

#endif // MICROPY_INCLUDED_LITEX_LITEETH_NETIF_H
