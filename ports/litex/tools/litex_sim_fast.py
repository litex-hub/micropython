#!/usr/bin/env python3
#
# Drop-in replacement for `python3 -m litex.tools.litex_sim` that loads the
# upstream litex_sim source, monkey-patches a few performance knobs, then
# runs the patched copy.
#
# Why we don't just upstream the knobs: litex.tools.litex_sim hard-codes
# sys_clk_freq = int(1e6) and doesn't expose the BIOS_NO_* flags via CLI
# args. Both are easy to rewire here without touching the user's LiteX
# install.
#
# The two patches together typically cut REPL-up time on the LiteX sim
# from ~30 s wall-clock to under 5 s.
#
# Two knobs (set via CLI flags this wrapper accepts in addition to all
# upstream litex_sim args):
#
#   --sys-clk-freq HZ        Reported SoC clock. Verilator's wall-clock
#                            cycle rate is fixed by the design, but every
#                            simulated-time delay (BIOS serialboot timeout,
#                            mp_hal_delay_us, time.sleep) is computed
#                            against this number. Lowering it from 1 MHz
#                            to 100 kHz makes those delays ~10x faster in
#                            wall-clock.
#
#   --skip-bios-boot         Inject CONFIG_BIOS_NO_BOOT into the SoC's
#                            generated software_constants.h so the BIOS
#                            skips the serialboot/flashboot/sdcardboot
#                            chain and jumps straight to ROM_BOOT_ADDRESS
#                            (the firmware loaded via --ram-init).
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import importlib.util
import re
import sys
import types


def patched_module(sys_clk_freq_hz, skip_bios_boot, with_uart1):
    spec = importlib.util.find_spec("litex.tools.litex_sim")
    src = open(spec.origin).read()

    # Patch the hard-coded sys_clk_freq. There are two assignments in
    # upstream — one inside SimSoC.__init__, one inside main() for
    # sim_config.add_clocker — keep them in sync.
    new = f"sys_clk_freq = int({int(sys_clk_freq_hz)})"
    src, n_patched = re.subn(r"sys_clk_freq\s*=\s*int\(1e6\)", new, src)
    if n_patched == 0:
        raise RuntimeError(
            "litex_sim_fast: did not find 'sys_clk_freq = int(1e6)' to patch — "
            "is your LiteX checkout newer than what this wrapper knows?"
        )

    if skip_bios_boot:
        # Upstream litex_sim has commented-out add_config calls right after
        # the SoCCore.__init__ for exactly this purpose:
        #
        #   # BIOS Config ------------------------------------------------
        #   # FIXME: Expose?
        #   #self.add_config("BIOS_NO_PROMPT")
        #   #self.add_config("BIOS_NO_DELAYS")
        #   #self.add_config("BIOS_NO_BUILD_TIME")
        #   #self.add_config("BIOS_NO_CRC")
        #
        # Just uncomment them. NO_DELAYS shrinks every BIOS timer to 0 so
        # serialboot/flashboot/sdcardboot fall through immediately;
        # NO_PROMPT skips the interactive shell; NO_BUILD_TIME / NO_CRC
        # cut a few more seconds of UART output. Together this drops
        # 5+ seconds of BIOS-time wait (= dozens of wall-clock seconds at
        # Verilator speeds) before MicroPython gets control.
        for flag in ("BIOS_NO_PROMPT", "BIOS_NO_DELAYS", "BIOS_NO_BUILD_TIME", "BIOS_NO_CRC"):
            pattern = rf'#self\.add_config\("{flag}"\)'
            replacement = f'self.add_config("{flag}")'
            src, n = re.subn(pattern, replacement, src, count=1)
            if n == 0:
                raise RuntimeError(
                    f"litex_sim_fast: did not find a commented "
                    f'#self.add_config("{flag}") to uncomment'
                )

    if with_uart1:
        # Inject a second UART block after the main SoCCore.__init__ runs.
        # uart_name="stub" gives us a UART with the standard CSR layout
        # (rxtx, txfull, rxempty, ev_*) and an always-ready sink — perfect
        # for exercising machine.UART(1) and UART.irq() in sim without
        # needing another TCP/PTY pair on the host. Writes are silently
        # acked; reads block (rxempty=1 always) which is exactly what
        # machine.UART.read_nb / .any() should report.
        injection = (
            "SoCCore.__init__(self, platform, clk_freq=sys_clk_freq, "
            'ident = "LiteX Simulation", **kwargs)\n        '
            'self.add_uart(name="uart1", uart_name="stub")'
        )
        original = (
            "SoCCore.__init__(self, platform, clk_freq=sys_clk_freq,\n"
            '            ident = "LiteX Simulation",\n'
            "            **kwargs)"
        )
        src, n = re.subn(re.escape(original), injection, src, count=1)
        if n == 0:
            raise RuntimeError(
                "litex_sim_fast: could not find SoCCore.__init__ call to "
                "inject the second UART after — has litex_sim been reformatted?"
            )

    mod = types.ModuleType("litex.tools.litex_sim_fast")
    mod.__file__ = spec.origin
    exec(compile(src, spec.origin, "exec"), mod.__dict__)
    return mod


def main():
    # Pull our extra args out of argv before deferring to litex_sim.main(),
    # which only knows about the upstream flags.
    parser = argparse.ArgumentParser(
        add_help=False,  # let litex_sim print its own help
        description=__doc__.splitlines()[1] if __doc__ else "",
    )
    parser.add_argument("--sys-clk-freq", type=lambda s: int(float(s)), default=100_000)
    parser.add_argument("--skip-bios-boot", action="store_true", default=False)
    parser.add_argument(
        "--with-uart1",
        action="store_true",
        default=False,
        help="Add a second 'stub' UART so machine.UART(1) "
        "exists in sim. CSR_UART1_BASE and "
        "UART1_INTERRUPT will be generated.",
    )
    args, rest = parser.parse_known_args()

    mod = patched_module(args.sys_clk_freq, args.skip_bios_boot, args.with_uart1)

    # litex_sim.main() reads sys.argv directly, so trim ours.
    sys.argv = [sys.argv[0]] + rest
    mod.main()


if __name__ == "__main__":
    main()
