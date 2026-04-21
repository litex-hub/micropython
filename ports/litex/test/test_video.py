# Simple Framebuffer test.
# Requirements:
#  - Framebuffer format should be "rgb565"
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause


import litex
from framebuf import FrameBuffer, RGB565

video = litex.Video(0)
W, H = video.width(), video.height()
print("Resolution: %dx%d" % (W, H))
fbuf = FrameBuffer(video, W, H, RGB565, video.stride())

fbuf.fill(0xFC00) #orange
fbuf.fill_rect(240,240,240,240,0x001F) #full blue
fbuf.fill_rect(120,120,240,240,0x07E0) #full green
fbuf.fill_rect(240,240,120,120,0xF800) #full red
fbuf.line(120,120,360-1,360-1,0) #black
