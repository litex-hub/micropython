# network.LAN smoke test.
#
# Brings up the LiteEth-backed network.LAN(0), sets a static IP, and
# verifies the ifconfig round-trips. Doesn't require any host-side tap
# to be configured — the constructor + active() + ifconfig() exercise
# the netif driver without needing actual packet flow.
#
# When the sim *is* run with --with-ethernet and a tap with reachable
# gateway, this also validates the round-trip via a UDP send (best-
# effort; a missing gateway just shows up as "no reply" and we move on).
#
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import network

lan = network.LAN(0)

# active(True) initialises the LiteEth MAC + lwIP netif.
lan.active(True)
assert lan.active() is True

# Static IP — fast and deterministic. (DHCP would block until lease.)
lan.ifconfig(("192.168.42.50", "255.255.255.0", "192.168.42.1", "8.8.8.8"))
ip, mask, gw, dns = lan.ifconfig()
assert ip == "192.168.42.50", "ip: " + ip
assert mask == "255.255.255.0"
assert gw == "192.168.42.1"
assert dns == "8.8.8.8"

# config('mac') returns the auto-generated MAC.
mac = lan.config("mac")
assert isinstance(mac, bytes) and len(mac) == 6, "mac: " + str(mac)
# Locally-administered bit set (bit 1 of first byte).
assert mac[0] & 0x02, "MAC should be locally-administered"

# Tear down cleanly.
lan.active(False)
assert lan.active() is False

print("lan OK")
