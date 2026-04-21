# Draw primitives into the LiteX video framebuffer using the stock
# `framebuf` module.
#
# litex.Video exposes the framebuffer memory through the buffer protocol
# (see litex_video.c), so `framebuf.FrameBuffer(video, w, h, format)` maps
# directly over the hardware framebuffer — no copies, every framebuf.pixel /
# .text / .fill call writes straight to screen RAM.
#
# Requires a LiteX SoC generated with --with-video-framebuffer (matching
# the resolution the script opens below).
#
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import framebuf
import litex

# Video object 0. Resolution and bit depth come from the SoC generation
# (VIDEO_FRAMEBUFFER_HRES / _VRES / _DEPTH), so this script works as-is on
# any --with-video-framebuffer build.
video = litex.Video(0)
w, h = video.width(), video.height()
bpp = video.bpp()

# Pick a framebuf colour format that matches the SoC bit depth.
if bpp == 16:
    fmt = framebuf.RGB565
    white = 0xFFFF
    red = 0xF800
elif bpp == 32:
    # No 32-bit RGBA format in framebuf — the 16-bit slot still works as
    # raw bit-manipulation, so fall back to that. Users wanting per-pixel
    # access at full depth can bypass framebuf and write via video.blitbuf().
    fmt = framebuf.RGB565
    white = 0xFFFF
    red = 0xF800
else:
    raise RuntimeError("unsupported video bpp: %d" % bpp)

fb = framebuf.FrameBuffer(video, w, h, fmt)

# Clear, then draw a border and a centered text label.
fb.fill(0)
fb.rect(0, 0, w, h, white)
fb.text("LiteX + MicroPython", w // 2 - 80, h // 2 - 4, red)
