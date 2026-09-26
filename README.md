# ESP32 Blackbox

[English](README.md) | [中文](README.zh.md)

A lightweight network probing device for ESP32, compatible with Prometheus blackbox_exporter. Configure probes via JSON or Web UI, and get metrics in standard Prometheus format.

## Supported Hardware

| Board | Chip | WiFi | Onboard LED |
|-------|------|------|-------------|
| ESP32-C3 SuperMini | ESP32-C3 160MHz | 802.11 b/g/n | GPIO8 (active-low) |
| Seeed Studio XIAO ESP32C6 | ESP32-C6 160MHz | 802.11ax (WiFi 6) | GPIO15 (active-high) |

## Quick Start

### Build & Flash

```bash
# Python script (recommended, cross-platform)
python build.py esp32c6 build
python build.py esp32c6 flash COM3

# Or use ESP-IDF CLI directly (after sourcing export.sh)
idf.py set-target esp32c6
idf.py build
idf.py -p COM3 flash monitor
```

### First Boot

1. Device creates WiFi hotspot `ESP32_Blackbox` (password: `12345678`)
2. Connect and open `http://192.168.4.1`
3. Scan networks, enter your WiFi credentials, save
4. Device reboots and connects to your WiFi

## Probe Endpoints

Once connected, the device exposes two HTTP ports:

| Port | Endpoint | Description |
|------|----------|-------------|
| 9090 | `/metrics` | Prometheus metrics for all configured targets |
| 9090 | `/probe?target=X&module=Y` | On-demand probe (no config needed) |
| 80 | `/` | Web dashboard for configuration |
| 80 | `POST /ota` | Firmware upload (OTA dual-slot, on both AP portal & STA dashboard; verified image swaps slot and reboots, invalid image keeps old firmware) |
| 80 | `POST /api/reboot` | Reboot device |

OTA / watchdog notes (2026-09-24): partition table switched to dual OTA slots
(`ota_0`/`ota_1` 1344K each + `otadata`); **`storage` (SPIFFS) moved to
0x2b0000 — first re-flash with the new table loses the on-device config JSON**
(re-provision via web UI). `probe_task` subscribes to the task watchdog with a
30s timeout (probes are synchronous network calls), panic → reboot self-recovery.
App binary currently leaves only ~10% headroom in a slot — shrink `storage`
first if it outgrows.

### On-demand Probing

Probe any target instantly without modifying config:

```bash
# HTTP probe
curl "http://192.168.61.150:9090/probe?target=httpbin.org&module=http_2xx"

# DNS resolution
curl "http://192.168.61.150:9090/probe?target=8.8.8.8&module=dns"

# TCP port check
curl "http://192.168.61.150:9090/probe?target=example.com&module=tcp&port=443"

# ICMP ping
curl "http://192.168.61.150:9090/probe?target=8.8.8.8&module=icmp_ping"
```

Returns standard Prometheus format:

```
probe_success{target="httpbin.org",module="http_2xx"} 1
probe_duration_seconds{target="httpbin.org",module="http_2xx"} 0.234
```

### Prometheus Integration

```yaml
scrape_configs:
  - job_name: 'blackbox'
    metrics_path: /probe
    params:
      module: [http_2xx]
    static_configs:
      - targets:
          - example.com
          - httpbin.org
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.61.150:9090
```

## Supported Probe Modules

| Module | Protocol | Description |
|--------|----------|-------------|
| `http_2xx` | HTTP/HTTPS | HTTP GET/POST with status code validation |
| `tcp` | TCP | TCP connection test |
| `dns` | DNS | DNS resolution test |
| `icmp_ping` | ICMP | ICMP Ping (native socket) |
| `ws` | WebSocket | WebSocket connection test |
| `wss` | WebSocket Secure | WebSocket + TLS |

## LED Status Indicator

| State | Pattern |
|-------|---------|
| Booting | Fast blink |
| AP Mode (config) | Slow blink |
| Connecting WiFi | Medium blink |
| Connected | Solid on |
| Disconnected | Fast blink |
| Connection Failed | 3x blink + 1s pause |

## Documentation

| Document | Description |
|----------|-------------|
| [Usage Guide](docs/en/usage.md) | Environment setup, build, flash, configuration, Prometheus integration |
| [Architecture](docs/en/architecture.md) | System architecture, module design, data flow |
| [Design](docs/en/design.md) | Technical decisions, probe module internals |

## License

MIT
