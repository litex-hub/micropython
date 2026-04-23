MicroPython port to the LiteX SoC FPGA framework
================================================

This is a port of MicroPython to the LiteX SoC FPGA framework. LiteX allows
easy creation of SoCs on FPGAs with various CPU ISAs/implementations
(VexRiscv, NaxRiscv, Mor1kx, ...) and peripherals. Combining LiteX's
flexibility for hardware definition with MicroPython's for control gives
a powerful, interactive bring-up environment.

The port tracks **upstream MicroPython 1.28** (latest stable).

Supported features:
- REPL (Python prompt) over UART.
- Standard `machine` module: `Pin`, `SPI`/`SoftSPI`, `SoftI2C`, `PWM`,
  `Timer`, `UART` (secondary UARTs), `SDCard`, `ADC` (Xilinx XADC),
  `mem8/16/32`, plus `reset()`, `freq()`, and the LiteX-specific
  `identifier()`. Peripheral classes are gated by the corresponding CSR,
  so `machine.Pin` only appears on SoCs built with a GPIO core, etc.
- Standard `time` and `os` modules with the LiteX timer HAL underneath.
- LiteX-specific `litex` module for build metadata (sys_clk_freq, git
  sha, bus standard), raw MMIO (`read32`/`write32`), by-name CSR access
  (`csr_read`/`csr_write`/`csrs` via a build-time lookup table),
  `EventManager` for per-peripheral event CSRs, and `info()`.
- Optional peripheral wrappers under `litex.*`: `LED`, `DMAWriter`,
  `DMAReader`, `Video` (the `Video` object plugs directly into
  `framebuf.FrameBuffer` for zero-copy drawing).
- FatFS over SD card (when the SoC includes an `SDCore` or `SPISDCard`).

Setting up LiteX
----------------

To install LiteX, please follow the [LiteX installation guide](https://github.com/enjoy-digital/litex/wiki/Installation).

Building your LiteX target
--------------------------
The port of MicroPython to LiteX relies on the software files generated during the target build
that will provides the hardware definition and mapping to MicroPython. To build the MicroPython
firmware, the LiteX target then first needs to be generated. Many FPGA boards are already available
in [LiteX-Boards](https://github.com/litex-hub/litex-boards), in this example, we'll use the Digilent
Arty board:

```bash
$ python3 -m litex_boards.targets.digilent_arty --with-ethernet --with-pmod-gpio --timer-uptime --build --load
```
This will build the FPGA SoC, generate the software headers, compile the BIOS/FPGA and load it to the board.

Building MicroPython for your LiteX target
------------------------------------------
To build MicroPython for your LiteX target run:
```bash
$ export BUILD_DIRECTORY=build/digilent_arty
$ make
```

Loading MicroPython to your LiteX target
----------------------------------------
To load MicroPython for your LiteX target run:
```bash
$ litex_term /dev/ttyUSBX --kernel=build/firmware.bin
```
You can also use TFTP boot from LiteX:
```bash
$ cp build/firmware.bin /tftpboot/boot.bin
```
And just let LiteX boot from it!...

..or use one of the other available boot methods described at https://github.com/enjoy-digital/litex/wiki/Load-Application-Code-To-CPU

Running tests against real hardware
-----------------------------------
With the MicroPython firmware booted on the SoC, the tests under `test/`
can be driven with the standard `pyboard.py` RAW-REPL client:
```bash
$ cd test
$ python3 ../../../tools/pyboard.py -d /dev/ttyUSBX test_hello_world.py
$ python3 ../../../tools/pyboard.py -d /dev/ttyUSBX test_machine.py
$ # ... etc.
```

Running tests under LiteX-sim (no FPGA needed)
----------------------------------------------
For quick iteration and CI, the port can be exercised entirely in software
against a Verilator-simulated LiteX SoC. This needs `verilator`, `socat`
and the `litex` Python package on your `$PATH`.

One-time: generate a sim target (this also produces the software headers
`ports/litex` needs for its build). The `--libc-mode=full` flag is required
because MicroPython uses `setjmp`, `memcmp`, math functions, etc. that are
absent from the `minimal` picolibc build:

```bash
$ python3 -m litex.tools.litex_sim \
      --cpu-type=vexriscv \
      --integrated-main-ram-size=0x01000000 \
      --libc-mode=full \
      --output-dir=/tmp/litex_mpy_sim \
      --no-compile-gateware
```

Build the MicroPython firmware against that target and run the sim smoke test:

```bash
$ cd ports/litex
$ export BUILD_DIRECTORY=/tmp/litex_mpy_sim
$ make
$ make test                                    # runs test/test_hello_world.py
$ make test TESTS="test/test_machine.py"       # or any other test
```

`make test` drives [`tools/run_sim.py`](tools/run_sim.py), which spawns
`litex_sim --uart-pty --non-interactive`, waits for the Verilator build
and the MicroPython REPL, then executes each test over the raw REPL.
The first run includes a one-time Verilator C++ compilation (~2 minutes on
a typical laptop); subsequent runs reuse the compiled `Vsim` binary.

A 16 MiB main RAM is the default because MicroPython zeroes a GC alloc
table proportional to the heap at startup and that scales linearly with
Verilator step rate. Passing `--ram-size=...` to `tools/run_sim.py`
overrides it.

The sim UART is a LiteX `RS232PHYModel` — a byte-level valid/ready stream
with no per-bit baud timing — so the effective throughput is whatever
Verilator can simulate, not 115200. `tools/run_sim.py` chunks its writes
to 64 bytes with a 50 ms inter-chunk pause so the firmware's 128-byte
libbase RX ring buffer always has room; hundreds-of-bytes test scripts
run comfortably this way. Hardware is straight RS-232 at whatever baud
the SoC was generated with (115200 by default).

The `litex` module
------------------

`litex` is the port's home for LiteX-specific helpers. Everything in it
is reflected from the SoC's generated C headers (`generated/csr.h`,
`generated/mem.h`, `generated/git.h`), so it tracks the actual SoC build
exactly — no hand-maintained duplication.

```python
>>> import litex
>>> litex.sys_clk_freq          # Hz
100000000
>>> litex.git_sha1()
'f377764d7'
>>> litex.bus_standard()
'wishbone'
>>> hex(litex.CSR_BASE())
'0xf0000000'
>>> litex.info()
LiteX SoC
  LiteX git: f377764d7
  bus:       wishbone
  clock:     100000000 Hz
  CSR base:  0xf0000000
  RAM base:  0x40000000 (size 0x10000000)
  ROM base:  0x00000000 (size 0x00020000)

>>> # Raw MMIO — useful during peripheral bring-up before a class exists.
>>> litex.read32(litex.CSR_BASE() + 0x1000)
0xcafe0001
>>> litex.write32(litex.CSR_BASE() + 0x1000, 0)

>>> # By-name CSR access — addresses come from a build-time lookup table
>>> # generated from the SoC's csr.json, so it stays in sync with the
>>> # hardware. Read-only CSRs raise on write, write-only on read.
>>> litex.csrs()[:3]
['ctrl_bus_errors', 'ctrl_reset', 'ctrl_scratch']
>>> litex.csr_write('ctrl_scratch', 42)
>>> litex.csr_read('ctrl_scratch')
42

>>> # EventManager — peripheral event CSR trio (<prefix>_ev_pending,
>>> # <prefix>_ev_enable, <prefix>_ev_status) wrapped by peripheral name.
>>> ev = litex.EventManager('uart')
>>> ev.pending()
0
>>> ev.enable(0x3)          # enable TX and RX events
>>> ev.clear(0x3)           # write-1-to-clear
```

Secondary UARTs with `machine.UART`
-----------------------------------

The primary LiteX UART is wired to the REPL. Any additional UARTs built
into the SoC (`--with-uart1`, `--with-uart2`, etc.) are exposed as
`machine.UART(id)` with the standard MicroPython stream API — `read`,
`write`, `readline`, `any`. Baud/parity/stop are fixed at SoC generation
time; the constructor raises on anything other than 8N1.

```python
import machine
u = machine.UART(1)
u.write(b"hello\n")
line = u.readline()
```

IRQ-driven RX is supported via `.irq()`:

```python
def on_rx(uart):
    print("got:", uart.read())

u.irq(on_rx, machine.UART.IRQ_RX)
# ...later:
u.irq(None)            # unregister
```

The handler runs in main-task context (scheduled via mp_sched_schedule
from the C-level isr() dispatcher in `litex_isr.h`). Same plumbing as
`litex.EventManager.irq()` — UART just knows its CPU IRQ bit and
ev_pending address at compile time.

System-monitor ADC
------------------

SoCs built with `--with-xadc` (or the equivalent SystemMonitor wrapper
on UltraScale / UltraScale+ / ZynqUSP parts) get `machine.ADC` backed
by the on-die monitor. Standard channels: `temperature`, `vccint`,
`vccaux`, `vccbram`; ZynqUSP adds `vccpsintlp`, `vccpsintfp`,
`vccpsaux`. Channels can be addressed by symbolic name, by numeric
index, or by raw CSR name (`machine.ADC('xadc_temperature')`) for
SoCs that wrap the core under a non-default prefix. `read()` returns
the raw N-bit sample; `read_u16()` returns the same value scaled into
the 0–65535 range used by other MicroPython ports.

```python
import machine
t = machine.ADC('temperature')
raw = t.read()
celsius = raw * 503.975 / 4096 - 273.15
```

Drawing into the framebuffer with `framebuf`
--------------------------------------------

`litex.Video` exposes the framebuffer memory through MicroPython's buffer
protocol, so the stock `framebuf.FrameBuffer` maps directly over it — no
copies, every primitive goes straight to screen RAM. Requires a SoC built
with `--with-video-framebuffer`.

```python
import framebuf, litex

video = litex.Video(0)
fb = framebuf.FrameBuffer(video, video.width(), video.height(),
                          framebuf.RGB565)
fb.fill(0)
fb.text("LiteX + MicroPython", 10, 10, 0xFFFF)
```

See [`examples/video_framebuf.py`](examples/video_framebuf.py) for a
ready-to-run script.

Networking with `network.LAN`
-----------------------------

SoCs built with `--with-ethernet` get a `network.LAN` interface backed
by LiteEth and lwIP. The standard MicroPython network API works
unchanged — anything that runs on stm32 / mimxrt / rp2 (urequests,
asyncio sockets, mqtt, webrepl, …) works here.

```python
import network

lan = network.LAN(0)
lan.active(True)
lan.ifconfig('dhcp')          # or a static (ip, mask, gw, dns) tuple
print(lan.ifconfig(), lan.config('mac'))

import socket
s = socket.socket()
s.connect(socket.getaddrinfo('example.com', 80)[0][-1])
s.send(b'GET / HTTP/1.0\r\nHost: example.com\r\n\r\n')
print(s.recv(4096))
```

The LiteEth driver currently polls RX from the VM loop and from
`mp_hal_delay_ms`. IRQ-driven dispatch (via `litex_isr_register` on
`ETHMAC_INTERRUPT`) is a follow-up that drops idle CPU usage but
doesn't change the API.

To exercise the integration in `litex_sim` you also need a host-side
tap interface:

```bash
sudo ip tuntap add dev tap0 mode tap user $USER
sudo ip addr add 192.168.42.1/24 dev tap0
sudo ip link set tap0 up
make test BUILD_DIRECTORY=/tmp/litex_mpy_sim TESTS=test/test_lan.py
```

Without the tap, `network.LAN(0).active(True)` still succeeds
(`ifconfig`/`config('mac')` return the static IP / MAC) but no packets
flow.

Hardware testing on Digilent Arty A7
------------------------------------

The Arty A7 is the reference board for this port. `tools/run_hw.py`
drives the full bring-up loop — load bitstream, SFL-upload firmware,
drop into raw REPL, run each test — over a single open UART fd. The
single-fd design is deliberate: closing the FTDI port toggles
DTR/RTS, which on Arty-class boards reaches the FPGA reset line and
would wipe SDRAM between upload and test.

```bash
# 0) Tools (one time):
#    - Vivado (or yosys+nextpnr-xilinx for an open-source flow)
#    - openFPGALoader  (https://github.com/trabucayre/openFPGALoader)
#    - python3-pyserial (for tools/run_hw.py)

# 1) Generate the SoC + synthesize the bitstream (~10–15 min on Vivado).
#    --with-ethernet / --with-xadc / --timer-uptime turn on the cores
#    test_hw_arty.py exercises.
python3 -m litex_boards.targets.digilent_arty \
    --build \
    --with-ethernet \
    --with-xadc \
    --timer-uptime \
    --cpu-type=vexriscv \
    --libc-mode=full \
    --output-dir=/tmp/arty_eth

# 2) Build the MicroPython firmware against the same SoC.
make -C ports/litex BUILD_DIRECTORY=/tmp/arty_eth -j$(nproc)

# 3) One shot: load the bitstream, upload firmware, drop into raw
#    REPL, run the tests. /dev/ttyUSB1 is the default UART;
#    ttyUSB0 is the JTAG channel.
ports/litex/tools/run_hw.py \
    --bitstream /tmp/arty_eth/gateware/digilent_arty.bit \
    --firmware  ports/litex/build/firmware.bin \
    ports/litex/test/test_hw_arty.py
```

Skip `--bitstream` when the FPGA is already loaded with a fresh
bitstream and the BIOS is in its console; skip `--firmware` when
the MicroPython REPL is already up.

`test/test_hw_arty.py` covers, end to end on real silicon: CSR
read/write, `litex.LED` blink, XADC temperature + vccint, timer IRQ
→ Python callback dispatch, and `network.LAN(0)` DHCP + DNS + a
plain HTTP GET against the LAN. The FTDI defaults match Arty's
factory wiring; override with `LITEX_HW_PORT=/dev/ttyUSBn` for other
serial devices.

### SDCard

For the SD round-trip test, generate the SoC with
`--with-sdcard --sdcard-adapter=digilent` (a Digilent PmodSD on
connector JD), insert a FAT/FAT32-formatted microSD, and run:

```bash
python3 -m litex_boards.targets.digilent_arty \
    --build --with-sdcard --sdcard-adapter=digilent \
    --cpu-type=vexriscv --libc-mode=full --output-dir=/tmp/arty_sd
make -C ports/litex BUILD_DIRECTORY=/tmp/arty_sd -j$(nproc)
ports/litex/tools/run_hw.py \
    --bitstream /tmp/arty_sd/gateware/digilent_arty.bit \
    --firmware  ports/litex/build/firmware.bin \
    ports/litex/test/test_hw_arty_sdcard.py
```

The test exercises `machine.SDCard()`, `ioctl(BLOCK_SIZE/COUNT)`,
`uos.mount('/sd')`, and a write+read+remove file round-trip through
FatFs + the SDCard block driver. It does *not* mkfs (so any existing
data on the card is preserved). `run_hw.py` pulses `Q` during the
BIOS boot wait so a leftover `boot.json` on the card doesn't
auto-execute before we can drop into the console.

Supported hardware
------------------

The port is intentionally agnostic about CPU and board: anything LiteX
can target works as long as the SoC is built with the right CSRs. The
firmware compiles against the SoC's `software/include/generated/` headers
(produced by the LiteX build), so the matrix below describes what has
been *exercised*, not what is *possible*.

**CPUs** — every CPU type supported by LiteX is in principle compilable;
the ones tested with this port:

| CPU            | Status     | Notes                                     |
| -------------- | ---------- | ----------------------------------------- |
| `vexriscv`     | primary    | Tested in sim and on hardware (Arty A7).  |
| `vexriscv_smp` | should work| Same ABI as `vexriscv`; not in CI yet.    |
| `naxriscv`     | should work| Needs SBT/Scala for SoC gen; CI omits it. |
| `mor1kx`       | legacy     | Compiled in the 1.16-era port; untested.  |
| `lm32`         | legacy     | Compiled in the 1.16-era port; untested.  |

**Peripherals** — the Python-visible classes are gated on the
corresponding CSRs, so a class only appears when the SoC was built with
that core enabled:

| Python class            | Required CSR / build flag       | Source        |
| ----------------------- | ------------------------------- | ------------- |
| `machine.Pin`           | `CSR_GPIO_BASE` (`--with-gpio`) | `machine_pin.c` |
| `machine.SPI`           | `CSR_SPI_BASE` / `CSR_SPI0_BASE` (`--with-spi`) | `machine_hw_spi.c` |
| `machine.Timer`         | `CSR_TIMER0_BASE` (always present) | `machine_timer.c` |
| `machine.PWM`           | `CSR_LEDS_PWM_ENABLE_ADDR` (`--with-led-chaser` + PWM) | `machine_pwm.c` |
| `machine.SDCard`        | `CSR_SDCORE_BASE` or `CSR_SPISDCARD_BASE` | `machine_sdcard.c` |
| `machine.UART(id)`      | `CSR_UART<N>_BASE` (extra `--with-uart`) | `machine_uart.c` |
| `machine.ADC(channel)`  | `CSR_XADC_*` / `CSR_SYSMON_*` (Xilinx XADC / SystemMonitor incl. ZynqUSP rails) | `machine_adc.c` |
| `machine.SoftSPI`       | always available (bit-bangs Pin)| `extmod`       |
| `machine.SoftI2C`       | always available (bit-bangs Pin)| `extmod`       |
| `litex.LED`             | `CSR_LEDS_BASE`                 | `litex_led.c`  |
| `litex.DMAReader/Writer`| `CSR_DMA_READER_BASE` / `CSR_DMA_WRITER_BASE` | `litex_dma.c` |
| `litex.Video`           | `CSR_VIDEO_FRAMEBUFFER_BASE` (`--with-video-framebuffer`) | `litex_video.c` |
| `litex.EventManager`    | any peripheral with `<prefix>_ev_*` CSRs | `modlitex.c` |
| `network.LAN`           | `CSR_ETHMAC_BASE` (`--with-ethernet`) | `network_lan.c` + `liteeth_netif.c` |

**Boards** — any [LiteX-Boards](https://github.com/litex-hub/litex-boards)
target works once you generate it with `--build`. The port is
specifically exercised on:

| Board            | LiteX-Boards target                | Notes                  |
| ---------------- | ---------------------------------- | ---------------------- |
| Digilent Arty A7 | `litex_boards.targets.digilent_arty` | Reference target; CI build. |
| Terasic DE0-Nano | `litex_boards.targets.terasic_de0nano` | No DRAM; firmware fits in SRAM only with care. |
| LiteX Sim        | `litex.tools.litex_sim`            | Verilator; used for CI. |

Architecture
------------

The port is a thin glue layer between LiteX's generated SoC description
and MicroPython's HAL. There is no hand-written hardware definition —
every register address, IRQ number, and peripheral capability is
discovered at firmware compile time from the headers LiteX emits.

```
SoC generation (Python, host)
   │  litex_boards.targets.<board> --build
   ▼
build/<board>/software/include/generated/
   ├── csr.h              # CSR_<PERIPH>_<REG>_ADDR macros
   ├── soc.h              # CONFIG_CLOCK_FREQUENCY, BUS_STANDARD, …
   ├── mem.h              # MAIN_RAM_BASE/SIZE, ROM_BASE/SIZE
   ├── git.h              # LITEX_GIT_SHA1
   ├── variables.mak      # CPU flags, library paths, CRT0 path
   └── csr.json           # JSON form, consumed by gen_csr_table.py
   │
   ▼
ports/litex (this port)
   ├── modlitex.c         # `litex` module — CSR/MMIO/EventManager
   ├── modmachine.c       # `machine` module — port hooks + EXTRA_GLOBALS
   ├── machine_*.c        # per-peripheral types, gated on CSR_*
   ├── litex_*.c          # LiteX-specific types (LED, DMA, Video)
   ├── isr.c              # CPU IRQ entry → libbase + litex_isr_dispatch
   ├── mphalport.{c,h}    # mp_hal_* hooks (delays, ticks, stdio)
   └── tools/
       ├── gen_csr_table.py     # csr.json → genhdr/litex_csr_table.h
       ├── litex_sim_fast.py    # litex_sim wrapper (faster sim)
       └── run_sim.py           # spawn sim + drive raw REPL
```

The chain a Python call goes through, end to end, for a peripheral access:

```
machine.Timer(0).callback(f)         # Python
   │
   ▼ MICROPY_PY_MACHINE_INCLUDEFILE → modmachine.c → machine_timer.c
   │
   ▼ libbase / direct CSR write
   │   timer0_load_write(...) → MMPTR(CSR_TIMER0_LOAD_ADDR) = ...
   │
   ▼ Hardware (Verilator or FPGA)
       on event: IRQ asserted → CPU enters isr()
   ▲
   │ isr.c → litex_isr_dispatch → mp_sched_schedule(f, owner)
   │
   ▼ MicroPython main task picks up scheduled callback
   f(owner)
```

Adding a new peripheral
-----------------------

The pattern for exposing a LiteX peripheral as a Python class:

1. **Pick a CSR you can detect on**. Every LiteX core emits a stable
   `CSR_<PERIPH>_BASE` macro into `csr.h`. Use it as the conditional in
   `mpconfigport.h` / `modmachine.c` / `Makefile` — the class only
   compiles in if the SoC was built with the core. No runtime probing.

2. **Write `<thing>.c` next to the existing peripherals**. Use
   `machine_pin.c` (small, type+method pattern) or `machine_uart.c`
   (stream protocol + IRQ) as templates. Define the type with
   `MP_DEFINE_CONST_OBJ_TYPE(...)`. Talk to the hardware through the
   generated CSR accessors (`<periph>_<reg>_read()`, `..._write()`) —
   never hard-code addresses.

3. **Hook it into the right module**:
   - `machine.*` types: add an `extern const mp_obj_type_t` and an
     `MACHINE_<NAME>_ENTRY` block in `modmachine.c`, then reference it
     from `MICROPY_PY_MACHINE_EXTRA_GLOBALS`.
   - `litex.*` types: add an `extern` and an entry in
     `litex_module_globals_table[]` in `modlitex.c`.

4. **Wire IRQs (optional)**. If the peripheral has a LiteX
   `EventManager` (`<prefix>_ev_pending/_enable/_status` CSRs), users
   get IRQ→Python dispatch for free via `litex.EventManager(prefix)`.
   For type-specific `.irq()` API (like `machine.UART.irq()`), call
   `litex_isr_register(bit, ev_pending_addr, handler, owner)` from your
   class — it takes care of CPU mask + scheduling.

5. **Add a sim-friendly test under `test/`**. If the peripheral is
   present in the `litex_sim` build (most non-physical cores are),
   it'll then be exercised by `make test` and CI.

Frozen Python modules
---------------------

The port ships an empty `manifest.py` so MicroPython 1.28's frozen
module machinery has something to point at. To freeze board-specific
Python helpers into the firmware:

```python
# ports/litex/manifest.py
include("$(MPY_DIR)/extmod/asyncio")    # any frozen library
freeze("$(PORT_DIR)/modules")           # your own .py files
```

The build picks this up automatically — `make` re-freezes whenever any
input under `manifest.py`'s tree changes. Frozen modules `import` like
any other Python module but live in ROM and don't consume heap.
