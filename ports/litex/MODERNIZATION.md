# LiteX port modernization plan

This document tracks the modernization of `ports/litex` against current upstream
MicroPython, and the infrastructure (simulation feedback loop, CI) needed to
sustain it.

Work happens on the `litex-modernize` branch, which now tracks
`upstream/master` (MicroPython 1.29). The pre-rebase state is kept on
`litex-modernize-1.16` for anyone who needs a 1.16-based build.

## Starting point (2026-04)

- Fork is based on MicroPython **1.16** (upstream `98c570302`, 2021-06-19).
- Upstream MicroPython is at **1.29.0** — ~4.5 years of drift.
- `master` is frozen at the 2021 commit; all LiteX work lives on `litex-rebase`.
  `ports/litex/` does not exist on `master`.
- Port is functionally complete: REPL over UART, `machine.{Pin, SPI, I2C, PWM,
  Timer, SDCard, mem32}`, `litex.{LED, DMA, Video}`, FatFS over SDCard,
  12 tests, 5 examples.
- No CI for the LiteX port.
- Makefile still references 2021 paths: `lib/utils/*`, `lib/mp-readline/*`,
  `lib/timeutils/*`. These have moved upstream (`shared/runtime/*`,
  `shared/readline/*`, `shared/timeutils/*`).

## Scope and non-goals

- **In scope**: modernize the port against current upstream MicroPython, add a
  LiteX-sim feedback loop, add GitHub Actions CI, improve the README, and
  add features for LiteX integrated cores.
- **Out of scope for now**: merging the port upstream
  (`micropython/micropython`). We may revisit once the port is cleanly on
  1.29 and has CI; until then, the fork is the home.
- **Out of scope**: CMake conversion. Upstream accepts Makefile-based ports
  (stm32, samd), and the LiteX build integrates cleanly with `variables.mak`.

## Workstreams

The work is split into four workstreams. §2 (sim) lands before §1 (rebase)
so the rebase has a fast feedback loop from day one.

### §2 — LiteX-sim feedback loop *(first)*

Use `litex_sim` (Verilator) with `--with-uart --uart=pty` to get a
`/dev/pts/N` that `pyboard.py` can drive exactly like a real board. Replaces
flash-and-reboot with a ~30 s Python-driven cycle.

Deliverables:
- `ports/litex/tools/run_sim.py` — spawn `litex_sim`, wait for the PTY line
  on sim stdout, run a `pyboard.py` test, compare output to `.py.exp`.
- `ports/litex/Makefile.test` (or a `test` target in the main Makefile) —
  modelled on `ports/qemu-arm/Makefile.test`.
- Sim build recipe: `python3 -m litex.tools.litex_sim --cpu-type=vexriscv
  --integrated-main-ram-size=0x10000000 --build` → produces
  `build/sim/software/include/generated/` that the port Makefile consumes.

Tests that don't require real hardware can all run in sim: `hello_world`,
`machine`, `time`, `timer`, `led`, `pin`, `i2c`, `pwm`, `dma`. SDCard tests
need `--with-sdcard` + a disk image — defer to a follow-up.

### §1 — Rebase onto upstream MicroPython 1.29

Non-exhaustive list of churn since 1.16:
- **Paths**: `lib/utils/*` → `shared/runtime/*`, `lib/mp-readline/*` →
  `shared/readline/*`, `lib/timeutils/*` → `shared/timeutils/*`.
- **Module registration**: `MICROPY_PORT_BUILTIN_MODULES` block in
  `mpconfigport.h` → `MP_REGISTER_MODULE(...)` at the bottom of each module's
  `.c` file.
- **Type definitions**: `mp_obj_type_t` literals → `MP_DEFINE_CONST_OBJ_TYPE(...)`.
- **`u`-prefix**: `umachine`/`utime`/`uos` → `machine`/`time`/`os`
  (upstream removed the `u`-aliases in 1.20).
- **extmod machine\***: restructured; port hooks into `shared/` instead of
  compiling `extmod/machine_spi.c` directly.
- **Manifests**: ports declare frozen Python modules via `manifest.py`.
- **Linting**: `tools/codeformat.py` + `codespell`.

Order, each step verified by a sim build + hello_world:
1. Update Makefile paths so it builds against 1.29-style tree.
2. Fix module registration + type macros.
3. Rename `umachine` → `machine` etc.
4. Codeformat pass, unify copyright headers.

Progress on branch `litex-1.29-rebase` (forked from `upstream/master`):
- [x] Port imported from `litex-modernize`.
- [x] Makefile `shared/` paths.
- [x] `#include` paths (including `extmod/modmachine.h` consolidation).
- [x] `STATIC` keyword → `static`.
- [x] `mp_hal_stdout_tx_strn` and `mp_lexer_new_from_file` signatures.
- [x] `modmachine.c` refactored to the `MICROPY_PY_MACHINE_INCLUDEFILE`
      pattern (callbacks + `MICROPY_PY_MACHINE_EXTRA_GLOBALS`).
- [x] `modutime.c` deleted; stock `extmod/modtime.c` enabled; HAL
      ticks/delay hooks moved to `mphalport.c`.
- [x] `modlitex.c` uses `MP_REGISTER_MODULE`; the old
      `MICROPY_PORT_BUILTIN_MODULES` block is gone.
- [x] `moduos.c` deleted; stock `extmod/modos.c` enabled via
      `MICROPY_PY_OS=1` and `MICROPY_PY_OS_UNAME=1`.
- [x] `mp_obj_type_t` literals → `MP_DEFINE_CONST_OBJ_TYPE` in all
      peripheral type files (`machine_pin`, `machine_hw_spi`,
      `machine_timer`, `machine_pwm`, `machine_sdcard`, `litex_dma`,
      `litex_video`, `litex_led`).
- [x] `machine_timer_obj_head` switched from `MICROPY_PORT_ROOT_POINTERS`
      to per-file `MP_REGISTER_ROOT_POINTER`.
- [x] Drop `u`-prefix from module names in tests and examples.
- [x] Avoid `long double` and `_Float16` codepaths (pin
      `MICROPY_FLOAT_FORMAT_IMPL_APPROX`, disable
      `MICROPY_FLOAT_USE_NATIVE_FLT16`); add one more compiler-rt
      helper (`floatundidf`) to satisfy 1.29's parser.
- [x] **Build + `test/test_hello_world.py` green on LiteX sim.**

Remaining polish (out of scope for the first "it runs" milestone):
- [ ] `tools/codeformat.py` pass + copyright header unification so the
      tree survives upstream's lint gate.
- [ ] Board-level `manifest.py` (currently none; required by 1.29's
      frozen-module machinery even if empty).
- [ ] Larger tests (`test_machine.py`, `test_time.py`, ...) exercise
      fine on hardware but time out on the simulated 1 MHz UART: they
      need `--timer-uptime` SoCs or a raw-paste REPL mode in
      `tools/run_sim.py` to be CI-viable. Not a port bug.
- [ ] Document board-level CPU clock override (sim is fixed at 1 MHz;
      the firmware can compute `mp_hal_delay_us` against
      `CONFIG_CLOCK_FREQUENCY` already, so real boards just work).

### §3 — GitHub Actions CI

One workflow, `.github/workflows/ports_litex.yml`, patterned after
`ports_qemu-arm.yml`:

- Install Verilator, `riscv64-unknown-elf-gcc`, Python, `litex`
  (via `litex_setup.py --init --install`).
- Generate a sim target, build the firmware, run the sim test harness from §2.
- Matrix on CPU variant (`vexriscv`, `vexriscv_smp`, `naxriscv`) for cheap
  CPU-abstraction coverage.
- 2–3 representative boards built (not flashed) to catch per-board breakage
  (`digilent_arty`, `terasic_de0nano`).

### §4 — README + feature additions

**README**: the quickstart and sim section are in place. Full rewrite
(supported CPUs/boards/peripherals as a table, architecture note
CSR → `generated/csr.h` → `mp_hal_*` → Python, "how to add a peripheral"
recipe, frozen modules via `manifest.py`) is deferred.

**Shipped this round**:
- [x] Extended the `litex` module with build metadata and MMIO helpers:
      `litex.sys_clk_freq`, `litex.CSR_BASE`, `litex.MAIN_RAM_BASE/SIZE`,
      `litex.ROM_BASE/SIZE`, `litex.git_sha1()`, `litex.bus_standard()`,
      `litex.read32()/write32()`, `litex.info()`.
- [x] Added `test/test_litex.py` to smoke-test the module.

**Feature additions, still pending** (rough priority order):
- `litex.CSR` — generic CSR read/write *by name* using
  `generated/csr.json`; a natural extension of the MMIO helpers above.
  Would likely pre-parse the JSON at build time into a C table.
- `machine.UART` for extra LiteX UARTs beyond the REPL one.
- `litex.EventManager` — expose IRQ sources as Python callbacks.
- `machine.ADC` — LiteXADC / Xilinx XADC wrappers.
- `framebuf` integration for Video, simple text console / primitives.
- `litex.Ethernet` / `socket` over LiteEth (big — needs lwIP; later pass).
- `litex.SATA`, `litex.PCIe` BAR access — niche but LiteX-differentiating.

## Working rules

- Small, focused commits; each commit keeps the build green (or is explicitly
  marked WIP).
- Commits use the existing prefix style: `ports/litex: <subject>`.
- No behaviour change in modernization commits where possible; split
  reformatting from semantic changes.
- All new Python follows `ruff format`; all new C follows `tools/codeformat.py`.
