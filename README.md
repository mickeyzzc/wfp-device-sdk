# wfp-device-sdk

**[English](README.md) | [中文](README.zh.md)**

**Shared board-side SDK home for the homepulse home-IoT platform** — the
common implementation of the WFP device protocol across MCUs (wfp-core
portable C99 + per-SDK ports + ESP common parts), plus the first board
project **blackbox** (an ESP32 network probing terminal). Target audience:
ESP32 (IDF), STM32 (HAL/Cube) and RP2040 (Pico SDK) board projects.

> Repo history: formed by merging the standalone `wfp-device-sdk` skeleton
> into the former `esp32-blackbox` repo (2026-09-30); old URLs redirect.

## Layout

```
wfp-device-sdk/
├── wfp-core/            # Portable C99: line protocol codec / hello·caps /
│   │                    # command dispatch / ACK·ERR / calibration — zero
│   ├── include/wfp.h    # SDK deps. Three port hooks + API (skeleton;
│   └── src/wfp.c        # finalized with the first real extraction)
├── ports/               # Per-SDK ports (future): thin adapters — serial/TCP
│                        # IO, kv=NVS/flash, millisecond clock
└── blackbox/            # First board project: ESP32 network probing
                         # terminal (Prometheus-compatible, self-contained)
```

**One repo, many functions, take only what you need**: each function is its
own directory with its own CMake target, and dependencies flow one way
(`esp/ → ports/ → wfp-core`). A project links only the components it uses —
unreferenced parts never get compiled. An STM32 or RP2040 board takes
`wfp-core` plus its own thin port and carries zero ESP code.

## The extraction ladder

1. **Isomorphic**: same-function code across board projects keeps identical
   names and structure (`main` / `app_<capability>` / protocol endpoint /
   console skeleton);
2. **Copy & adapt**: the second board copies verbatim, then adapts;
3. **Extract**: when a third board needs it or the logic stabilizes, it moves
   into this repo — **extracting too early freezes experimental code**.

wfp-core is currently a skeleton, filled in as the first real extraction
happens; no code is written ahead of real hardware.

## wfp-core port hooks (only three)

| Hook | Purpose | Per-platform landing |
|---|---|---|
| `send_line` | Emit one line (telemetry/ACK/ERR) | ESP: usb_serial_jtag/UART/TCP; STM32: CDC/UART; RP2040: stdio_usb |
| `now_ms` | Monotonic milliseconds (`t_ms` field) | Each SDK's tick |
| `kv_load/kv_store` | Persistent key-value (calibration etc.) | ESP: NVS; STM32: flash page; RP2040: sdk flash |

devid is supplied by each port implementer (ESP=MAC/efuse, STM32=96-bit UID,
RP2040=board unique ID). Protocol contract: `docs/wfp-protocol.md` in the
homepulse platform repo (not public yet; link to be added).

## The blackbox project

An ESP32 network probing terminal — Prometheus blackbox_exporter compatible
(ICMP/TCP/HTTP/DNS/WS probes + metrics endpoint + web config + OTA).
Supports the ESP32-C3 SuperMini and the Seeed XIAO ESP32-C6. See
[blackbox/README.md](blackbox/README.md).

## Consumption

Start by **copying into your board project** (each board stays independently
buildable); decide the distribution mechanism later (ESP-IDF component
manager / CMake FetchContent / git subtree).
