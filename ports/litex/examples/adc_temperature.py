# machine.ADC demo: read the on-die XADC system monitor on Xilinx parts
# (Artix-7, Kintex-7, etc.) and convert the raw 12-bit sample into
# degrees Celsius.
#
# Requires a SoC built with --with-xadc (CSR_XADC_TEMPERATURE present).
# On UltraScale / UltraScale+ / ZynqUSP the equivalent core is
# SystemMonitor — same channel names work.
#
# The XADC datasheet gives:
#   T_C = raw * 503.975 / 4096 - 273.15
# vccint / vccaux / vccbram are scaled the same way (raw / 4096) but in
# units of "3 V full scale", so volts = raw * 3 / 4096.
#
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import time
import machine

temp = machine.ADC("temperature")
vccint = machine.ADC("vccint")
vccaux = machine.ADC("vccaux")

while True:
    t_raw = temp.read()
    t_c = t_raw * 503.975 / 4096 - 273.15
    print("die %.1f C  vccint %.2f V  vccaux %.2f V" % (
        t_c,
        vccint.read() * 3 / 4096,
        vccaux.read() * 3 / 4096,
    ))
    time.sleep(1)
