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
- [x] `tools/codeformat.py` pass — uncrustify 0.72 + ruff format both
      shipped, tree is now lint-clean against upstream's gate.
- [x] Larger tests work in sim — `tools/run_sim.py` chunks raw-REPL
      writes (64-byte chunks + 50 ms inter-chunk pause) so the
      firmware's libbase RX ring buffer doesn't overflow.
      `test_litex.py` (~660 bytes) and `test_irq.py` round-trip
      cleanly. The 1 MHz / 100 kHz sim clock no longer constrains
      script length.
- [x] Sim wall-clock perf — `tools/litex_sim_fast.py` lowers the
      reported sys_clk_freq to 100 kHz and uncomments LiteX's
      `BIOS_NO_DELAYS` / `_PROMPT` / `_BUILD_TIME` / `_CRC` configs,
      cutting REPL-up time from ~30 s to under 5 s.
- [ ] Board-level `manifest.py` (currently none; required by 1.29's
      frozen-module machinery even if empty).

### §3 — GitHub Actions CI

`.github/workflows/ports_litex.yml` is in place and patterned after
`ports_qemu.yml`. Helpers live in `tools/ci.sh` (`ci_litex_setup`,
`ci_litex_build_sim`, `ci_litex_build_board`).

Shipped:
- [x] Install Verilator, riscv toolchain, LiteX (via `litex_setup.py
      --init --install --config=standard`) inside `ci_litex_setup`.
- [x] `build_and_test_sim` job: generates sim SoC via `litex_sim_fast.py`
      with the same args as `tools/run_sim.py`, builds firmware, runs
      the sim-safe test set (`hello_world`, `machine`, `litex`, `irq`,
      `uart`).
- [x] `build_boards` job: invokes `litex_boards.targets.<board> --build
      --no-compile-gateware --libc-mode=full` to produce headers + LiteX
      libraries without an FPGA toolchain, then links MicroPython
      firmware against them. Verifies per-board breakage without
      requiring Vivado/Yosys in CI.

Pending follow-ups:
- [ ] Extend the CPU matrix beyond `vexriscv` (`vexriscv_smp` should
      slot in cheaply; `naxriscv` needs SBT/Scala which is too heavy).
- [ ] Extend the board matrix beyond `digilent_arty`
      (`terasic_de0nano`, …) as we confirm they link cleanly.
- [ ] Cache `~/.local` from `litex_setup.py --install` to cut ~3 min
      off cold runs.

### §4 — README + feature additions

**README**: full rewrite landed. Quickstart, sim section, supported
hardware tables (CPU / peripheral / board), architecture diagram
(CSR → generated/csr.h → mp_hal → Python), "adding a new peripheral"
recipe, and frozen modules via `manifest.py` are all in place.

**Shipped across sessions**:
- [x] Extended the `litex` module with build metadata and MMIO helpers:
      `litex.sys_clk_freq`, `litex.CSR_BASE`, `litex.MAIN_RAM_BASE/SIZE`,
      `litex.ROM_BASE/SIZE`, `litex.git_sha1()`, `litex.bus_standard()`,
      `litex.read32()/write32()`, `litex.info()`.
- [x] By-name CSR access: `litex.csr_read(name)`, `litex.csr_write(name, v)`,
      `litex.csrs()`. Backed by a build-time-generated lookup table
      (`tools/gen_csr_table.py`) populated from the SoC's `csr.json`.
- [x] `litex.EventManager(prefix)` wrapping the
      `<prefix>_ev_pending/_ev_enable/_ev_status` CSR trio
      (polling-style; IRQ-dispatched callbacks are a follow-up).
- [x] `machine.UART(id)` for secondary LiteX UARTs (primary stays with
      REPL). Full MicroPython stream protocol (read / readline / write).
      Polling for now; litex.EventManager integration is the next step.
- [x] `machine.ADC` for the Xilinx XADC system monitor (temperature,
      vccint, vccaux, vccbram). `read()` / `read_u16()`.
- [x] `framebuf` integration for `litex.Video` — the Video type already
      exposed the buffer protocol; this pass adds an example and doc.
- [x] `test/test_litex.py` covers the new litex module API.
- [x] Empty `manifest.py` stub so `FROZEN_MANIFEST` is ready when a
      board-level variant wants to freeze .py modules.

**Feature additions, still pending** (rough priority order):
- `litex.Ethernet` / `socket` over LiteEth (big — needs lwIP; later pass).
- `litex.SATA`, `litex.PCIe` BAR access — niche but LiteX-differentiating.
- Non-Xilinx ADC cores (LiteADC) — current `machine.ADC` has a generic
  read path but the channel-detection enum needs a common base class
  refactor before LiteADC slots in cleanly.

## Working rules

- Small, focused commits; each commit keeps the build green (or is explicitly
  marked WIP).
- Commits use the existing prefix style: `ports/litex: <subject>`.
- No behaviour change in modernization commits where possible; split
  reformatting from semantic changes.
- All new Python follows `ruff format`; all new C follows `tools/codeformat.py`.
