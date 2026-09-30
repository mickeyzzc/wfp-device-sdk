# wfp-device-sdk

**[English](README.md) | [中文](README.zh.md)**

**homepulse 家庭智能物联网平台的板端共享 SDK 之家**——WFP 设备协议在各
MCU 上的公共实现（wfp-core 可移植 C99 + 各 SDK 移植层 + ESP 公共件），
外加首个板端项目 **blackbox**（ESP32 网络探测终端）。服务对象：ESP32
（IDF）、STM32（HAL/Cube）、RP2040（Pico SDK）三类板子项目。

> 仓史：本仓由原 `esp32-blackbox` 仓并入独立骨架仓 `wfp-device-sdk`
> 合并而成（2026-09-30），旧 URL 自动重定向。

## 布局

```
wfp-device-sdk/
├── wfp-core/            # 可移植 C99：行协议编解码 / hello·caps / 命令分发 /
│   │                    # ACK·ERR / 校准应用 —— 零 SDK 依赖
│   ├── include/wfp.h    # 三个移植口 + API（骨架，随首个真实抽取定稿）
│   └── src/wfp.c
├── ports/               # 各 SDK 的移植层（将来）：薄适配——串口/TCP 收发、
│                        # kv=NVS/flash、毫秒钟
└── blackbox/            # 首个板端项目：ESP32 网络探测终端
                         # （Prometheus blackbox 兼容，独立可编译）
```

**一仓多功能、按需取用**：每个功能 = 独立目录 + 独立 CMake target，依赖
方向单向（`esp/ → ports/ → wfp-core`）。项目只链接自己要的组件——没人
引用的部分不参与编译。STM32 / RP2040 板只取 `wfp-core` + 自写薄 port，
不背任何 ESP 代码。

## 抽库梯子

1. **同构**：各板项目同功能代码同名同构（`main` / `app_<能力>` / 协议端点 /
   控制台统一骨架）；
2. **拷贝改**：第二块板复用时逐字拷贝再改；
3. **抽库**：第三块板要用、或逻辑稳定后，移入本仓——**抽早了会把试验
   代码冻住**。

当前 wfp-core 为骨架，按梯子随首个真实抽取填充，不预写没上过真机的代码。

## wfp-core 移植口（仅三个）

| 移植口 | 用途 | 各平台落点 |
|---|---|---|
| `send_line` | 输出一行（遥测/ACK/ERR） | ESP: usb_serial_jtag/UART/TCP；STM32: CDC/UART；RP2040: stdio_usb |
| `now_ms` | 单调毫秒（`t_ms` 字段） | 各 SDK 的 tick |
| `kv_load/kv_store` | 校准等持久键值 | ESP: NVS；STM32: flash 页；RP2040: sdk flash |

devid 由各端口实现者提供（ESP=MAC/efuse、STM32=96bit UID、RP2040=板级
唯一 ID）。协议契约：homepulse 平台仓 `docs/wfp-protocol.md`（平台仓
暂未公开，公开后补链）。

## blackbox 项目

ESP32 网络探测终端——Prometheus blackbox_exporter 兼容（ICMP/TCP/HTTP/
DNS/WS 探测 + 指标端点 + Web 配置 + OTA）。支持 ESP32-C3 SuperMini 与
Seeed XIAO ESP32-C6。详见 [blackbox/README.zh.md](blackbox/README.zh.md)。

## 消费方式

起步**拷贝入板项目**（各板独立可编译原则不变）；成熟后再定分发机制
（ESP-IDF component manager / CMake FetchContent / git subtree）。
