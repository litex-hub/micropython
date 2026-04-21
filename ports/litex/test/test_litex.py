# Smoke test for the litex module (ports/litex/modlitex.c).
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import litex

# Constants and callables must be present and non-empty / positive.
assert litex.sys_clk_freq > 0
assert litex.CSR_BASE > 0
assert len(litex.git_sha1()) > 0
assert len(litex.bus_standard()) > 0

# Round-trip MMIO via ctrl.scratch (present on every LiteX SoC).
litex.write32(litex.CSR_BASE + 0x4, 0xdeadbeef)
assert litex.read32(litex.CSR_BASE + 0x4) == 0xdeadbeef

print("litex OK")
