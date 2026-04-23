# Hardware smoke test for Digilent Arty A7 (the reference board for the
# litex-modernize port). Exercises every piece of new functionality
# added during the port modernization, end to end on real silicon:
#
#   * litex.info() / .csrs() / .csr_read() / .csr_write() — build
#     metadata + CSR-by-name reflection
#   * litex.LED — visible blink
#   * machine.ADC('xadc_temperature') / 'vccint' — XADC readback
#   * machine.Timer(0) + litex.EventManager('timer0').irq() —
#     IRQ → Python callback dispatch via the C-level isr()
#   * network.LAN(0) — bring-up + DHCP off the local LAN, DNS
#     resolution, TCP HTTP GET
#
# Designed to be run via tools/run_hw.py over the Arty's FTDI UART:
#
#     tools/run_hw.py test/test_hw_arty.py
#
# Build/load instructions are in the README's "Hardware testing on
# Digilent Arty A7" section. Requires a SoC built with --with-ethernet
# --with-xadc --timer-uptime.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import time
import litex
import machine
import network
import socket


# ---------------------------------------------------------------------------
# 1) Build metadata / CSR reflection
# ---------------------------------------------------------------------------

print("=== litex module ===")
litex.info()
csrs = litex.csrs()
assert "ctrl_scratch" in csrs, "no ctrl_scratch CSR — wrong SoC?"
assert "xadc_temperature" in csrs, "XADC absent — rebuild with --with-xadc"
assert "ethmac_sram_writer_slot" in csrs, "ETHMAC absent — rebuild with --with-ethernet"

# CSR scratch round-trip — proves csr_read/write reach real hardware.
litex.csr_write("ctrl_scratch", 0xCAFEBABE)
got = litex.csr_read("ctrl_scratch")
assert got == 0xCAFEBABE, "scratch round-trip: 0x%08x" % got
print("scratch round-trip OK")

# ---------------------------------------------------------------------------
# 2) LED blink — visible on the board.
# ---------------------------------------------------------------------------

print("=== LED ===")
led = litex.LED(0)
for _ in range(4):
    led.on()
    time.sleep_ms(120)
    led.off()
    time.sleep_ms(120)
print("LED blink OK")

# ---------------------------------------------------------------------------
# 3) XADC — temperature + vccint should be in plausible ranges.
# ---------------------------------------------------------------------------

print("=== XADC ===")
t = machine.ADC("temperature").read()
v = machine.ADC("vccint").read()
# 12-bit raw; XADC temp formula is raw*503.975/4096 - 273.15. Plausible
# die temp 10..90 C → raw 2287..2939. We just sanity-check non-zero.
assert 1000 < t < 4096, "temperature raw: %d" % t
assert 1000 < v < 4096, "vccint raw: %d" % v
print("temp raw=%d, vccint raw=%d" % (t, v))
print("XADC OK")

# ---------------------------------------------------------------------------
# 4) Timer + EventManager IRQ → Python callback.
# ---------------------------------------------------------------------------

print("=== timer IRQ ===")
litex.csr_write("timer0_load", 100000)  # one-shot, ~1 ms at 100 MHz
litex.csr_write("timer0_reload", 0)
litex.csr_write("timer0_en", 1)

ev = litex.EventManager("timer0")
n_fired = [0]


def on_timer(owner):
    n_fired[0] += 1


ev.irq(on_timer)
ev.enable(1)

# Spin so the scheduled callback has a chance to run between bytecodes.
for _ in range(2000):
    pass

ev.irq(None)
assert n_fired[0] >= 1, "timer IRQ never fired"
print("timer IRQ fired %d time(s)" % n_fired[0])

# ---------------------------------------------------------------------------
# 5) Ethernet: DHCP, DNS, TCP. The board needs a real LAN with a DHCP
#    server reachable. example.com over plain HTTP is a stable target
#    that doesn't require TLS.
# ---------------------------------------------------------------------------

print("=== network.LAN ===")
lan = network.LAN(0)
lan.active(True)
lan.ifconfig("dhcp")

# Wait up to 10 s for DHCP to bind an address.
deadline = time.ticks_add(time.ticks_ms(), 10_000)
while not lan.isconnected() and time.ticks_diff(deadline, time.ticks_ms()) > 0:
    time.sleep_ms(200)
assert lan.isconnected(), "DHCP did not bind in 10 s — check LAN cable"

ip, mask, gw, dns = lan.ifconfig()
print("DHCP -> ip=%s gw=%s dns=%s" % (ip, gw, dns))
assert ip != "0.0.0.0"

# DNS resolution.
addr = socket.getaddrinfo("example.com", 80)[0][-1]
print("example.com -> %s" % (addr,))

# TCP HTTP GET.
s = socket.socket()
s.settimeout(5)
s.connect(addr)
s.send(b"GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
chunk = s.recv(64)
s.close()
assert chunk.startswith(b"HTTP/"), "unexpected HTTP reply: %r" % chunk
print("HTTP GET OK: %r..." % chunk[:24])

lan.active(False)

print("hw_arty OK")
