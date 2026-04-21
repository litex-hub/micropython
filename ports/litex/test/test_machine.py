# Simple Machine test.
# Copyright (C) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import gc
import umachine

# Display LiteX SoC identifier.
print(umachine.identifier().decode("utf-8"))

# Display sys_clk_freq.
print("sys_clk_freq: {}MHz".format(umachine.freq()/1e6))

# Display free memory.
gc.collect()
print("free memory: {}KiB".format(gc.mem_free()/1024))
