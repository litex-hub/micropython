# network.LAN demo: bring up the LiteEth interface, get a DHCP lease,
# and do a single HTTP GET against example.com.
#
# Requires a SoC built with --with-ethernet (i.e. CSR_ETHMAC_BASE
# present). Plug in a regular RJ45 cable to a LAN with a DHCP server
# reachable; no static IP needed.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import time
import network
import socket

lan = network.LAN(0)
lan.active(True)
lan.ifconfig("dhcp")

# Wait for DHCP — usually <1 s on a healthy LAN.
deadline = time.ticks_add(time.ticks_ms(), 10_000)
while not lan.isconnected() and time.ticks_diff(deadline, time.ticks_ms()) > 0:
    time.sleep_ms(200)
if not lan.isconnected():
    raise RuntimeError("DHCP failed — check the LAN cable and a DHCP server is reachable")

ip, mask, gw, dns = lan.ifconfig()
print("IP:", ip, "GW:", gw, "DNS:", dns, "MAC:", lan.config("mac"))

addr = socket.getaddrinfo("example.com", 80)[0][-1]
print("example.com ->", addr)

s = socket.socket()
s.settimeout(5)
s.connect(addr)
s.send(b"GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
# Drain in chunks until the server closes — the response body is small.
while True:
    chunk = s.recv(512)
    if not chunk:
        break
    print(chunk.decode("utf-8", "replace"), end="")
s.close()
