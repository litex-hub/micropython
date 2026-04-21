# Simple PWM test.
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

from umachine import PWM
pwm = PWM(0)
pwm.init(5, 10*1000*1000) #freq(Hz) = 5, duty(nanosec) = 10ms
print(pwm)
pwm.duty_u16(65536//2) #set duty = 50%
pwm.deinit() #all off

