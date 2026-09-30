[English](../en/usage.md) | **中文**

# ESP32 Blackbox Usage Guide

## Table of Contents

- [Environment Setup](#environment-setup)
- [Supported Development Boards](#supported-development-boards)
- [Build and Flash](#build-and-flash)
- [Initial Configuration (AP Mode)](#initial-configuration-ap-mode)
- [Configuration File Format](#configuration-file-format)
- [Web UI Configuration](#web-ui-configuration)
- [Verification and Operation](#verification-and-operation)
- [Hardware Self-Test](#hardware-self-test)
- [Prometheus Integration](#prometheus-integration)
- [Troubleshooting](#troubleshooting)

## Environment Setup

### 1. Install ESP-IDF v6.0

```bash
# Clone ESP-IDF v6.0
git clone --recursive -b v6.0 https://github.com/espressif/esp-idf.git

# Install dependencies
./install.sh

# Activate environment
. ./export.sh
```

Requires ESP-IDF **v6.0** (mbedTLS v4 + PSA Crypto API).

### 2. Verify Environment

```bash
idf.py --version
```

## Supported Development Boards

| Project | ESP32-C3 SuperMini | Seeed XIAO ESP32C6 |
|---------|-------------------|-------------------|
| Chip | ESP32-C3 (QFN32) | ESP32-C6FH4 (QFN32) |
| Architecture | RISC-V single-core 160MHz | RISC-V dual-core (HP 160MHz + LP 20MHz) |
| SRAM | 400KB | 512KB |
| Flash | 4MB (embedded) | 4MB (embedded) |
| WiFi | 802.11 b/g/n | 802.11ax (WiFi 6) |
| BLE | Bluetooth 5 LE | Bluetooth 5.3 LE |
| Others | - | Zigbee, Thread (IEEE 802.15.4) |
| USB | USB-Serial/JTAG | USB-Serial/JTAG |
| Self-test | Default off | Default on |
| Partition Table | Default (1MB app) | Custom (1.875MB app) |

## Build and Flash

### Method 1: Python Build Script (Recommended)

No need to manually run ESP-IDF export.bat, the script automatically configures toolchain paths:

```bash
# ESP32-C6 Build and Flash
python build.py esp32c6 flash COM6

# ESP32-C3 Build and Flash
python build.py esp32c3 flash COM3

# Build only (no flash)
python build.py esp32c6 build

# Clean and rebuild
python build.py esp32c6 clean
```

**Complete Parameters**:
```
python build.py <target> [action] [port]

target:  esp32c3 | esp32c6
action:  build | flash | monitor | clean  (default: build)
port:    COM port number, e.g., COM3, COM6       (default: auto-detect)
```

### Method 2: ESP-IDF Command Line

Need to run ESP-IDF export.bat first to activate environment:

```bash
# First build - Set target chip (automatically executes fullclean + loads sdkconfig.defaults.<target>)
idf.py set-target esp32c6        # or esp32c3

# Build
idf.py build

# Flash
idf.py -p COM6 flash

# Flash + Serial Monitor (Ctrl+] to exit)
idf.py -p COM6 flash monitor

# Clean
idf.py fullclean
```

### Method 3: Prebuilt Firmware (GitHub Releases)

Firmware binaries are automatically built for both targets on each release.

1. Download from [GitHub Releases](https://github.com/micke/esp32-blackbox/releases)
2. Flash using esptool:

```bash
# ESP32-C3
esptool.py --chip esp32c3 -p COM3 -b 460800 write_flash \
  0x0 bootloader-esp32c3.bin \
  0x8000 partition-table-esp32c3.bin \
  0x10000 esp32-blackbox-esp32c3.bin

# ESP32-C6
esptool.py --chip esp32c6 -p COM3 -b 460800 write_flash \
  0x0 bootloader-esp32c6.bin \
  0x8000 partition-table-esp32c6.bin \
  0x10000 esp32-blackbox-esp32c6.bin
```

### Switch Target Chips

When switching from C3 to C6 (or vice versa), **must set the target again**:

```bash
idf.py set-target esp32c6    # Automatically executes fullclean + regenerates sdkconfig
```

ESP-IDF automatically loads the corresponding `sdkconfig.defaults.<target>` file.

### SDKconfig Layered Mechanism

ESP-IDF build system loads configuration in the following order:

```
sdkconfig.defaults              ← Common configuration (WiFi AP, FreeRTOS, LWIP, etc.)
sdkconfig.defaults.<target>     ← Target-specific configuration (Flash, crypto, partition table)
```

| File | Content |
|------|---------|
| `sdkconfig.defaults` | WiFi AP SSID/password, retry count, FreeRTOS, LWIP, log level |
| `sdkconfig.defaults.esp32c3` | C3 Flash 4MB QIO, hardware crypto acceleration |
| `sdkconfig.defaults.esp32c6` | C6 Flash 4MB QIO, hardware crypto acceleration, custom partition table |

### COM Port Identification

View available serial ports on Windows:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

General rules:
- XIAO ESP32C6 usually appears on `COM5` ~ `COM8`
- ESP32-C3 SuperMini usually appears on `COM3` ~ `COM6`
- Only connect one board at a time to avoid port confusion

## Initial Configuration (AP Mode)

When the device first boots, if there are no WiFi credentials in NVS, it automatically enters AP configuration mode.

### Steps

1. **Power on the device**, wait about 5 seconds for boot completion
2. **Open phone/computer WiFi settings**, search and connect to the following hotspot:
   - SSID: `ESP32_Blackbox`
   - Password: `12345678`
3. **Open browser**, visit `http://192.168.4.1`
4. **Click "Scan WiFi"** button, wait for nearby WiFi networks to load
5. **Select your WiFi**, enter password
6. **Click "Save and Restart"**
7. Device automatically saves credentials to NVS and restarts
8. After restart, device enters STA mode and automatically connects to your configured WiFi

### Configuration Page Features

- WiFi scanning: Lists nearby WiFi networks with signal strength
- Manual input: Can also manually enter SSID (for hidden WiFi)
- Signal indicator: Shows signal strength of each network
- Save and restart: Writes credentials to NVS, device restarts and enters STA mode

### Reconfiguration

If you need to reconfigure WiFi, there are two ways:

**Method 1: Erase NVS (Recommended)**
```bash
idf.py -p COM6 erase-flash
idf.py -p COM6 flash
```

**Method 2: Press Reset Button**
- Hold RST/EN button while device restarts to enter reconfiguration mode

## Configuration File Format

ESP32 Blackbox uses SPIFFS filesystem to store JSON configuration files. Configuration file path is `/spiffs/blackbox.json`.

### Configuration File Structure

```json
{
  "modules": {
    "http_2xx": {
      "prober": "http",
      "timeout": 10,
      "http": {
        "method": "GET",
        "valid_status_codes": [200],
        "no_follow_redirects": false
      }
    },
    "tcp_connect": {
      "prober": "tcp",
      "timeout": 5,
      "tcp": {
        "tls": false,
        "query": "",
        "expected_response": ""
      }
    },
    "icmp_ping": {
      "prober": "icmp",
      "timeout": 5,
      "icmp": {
        "packets": 3,
        "payload_size": 56,
        "pattern": 0
      }
    }
  },
  "targets": [
    {
      "name": "httpbin_test",
      "target": "httpbin.org",
      "port": 80,
      "module": "http_2xx",
      "interval": 30
    },
    {
      "name": "dns_server",
      "target": "8.8.8.8",
      "port": 53,
      "module": "dns_resolve",
      "interval": 30
    },
    {
      "name": "gateway_ping",
      "target": "192.168.1.1",
      "port": 0,
      "module": "icmp_ping",
      "interval": 60
    }
  ],
  "scrape_interval": 30,
  "metrics_port": 9090
}
```

### Field Descriptions

#### Global Configuration Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| scrape_interval | number | No | 30 | Scrape interval (seconds) |
| metrics_port | number | No | 9090 | Metrics server port |

#### Module Configuration (modules)

Module configuration defines specific parameters for various probing protocols.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| prober | string | Yes | Module type: http, https, tcp, tcp_tls, dns, icmp, ws, wss |
| timeout | number | No | Timeout duration (seconds) |
| http | object | No | HTTP module specific configuration |
| tcp | object | No | TCP module specific configuration |
| dns | object | No | DNS module specific configuration |
| icmp | object | No | ICMP module specific configuration |

**HTTP Module Configuration**:
```json
  "prober": "http",
  "timeout": 10,
  "http": {
    "method": "GET",
    "valid_status_codes": [200, 301, 302],
    "no_follow_redirects": false
  }
}
```

Field descriptions:
- method: HTTP method (GET, HEAD, POST, PUT, DELETE)
- valid_status_codes: Array of valid HTTP status codes
- no_follow_redirects: Whether to not follow redirects

**TCP Module Configuration**:
```json
  "prober": "tcp",
  "timeout": 5,
  "tcp": {
    "tls": false,
    "query": "",
    "expected_response": ""
  }
}
```

Field descriptions:
- tls: Whether to use TLS (only valid for tcp_tls type)
- query: Query string to send
- expected_response: Expected response string

**DNS Module Configuration**:
```json
  "prober": "dns",
  "timeout": 5,
  "dns": {
    "query_name": "httpbin.org",
    "query_type": 1
  }
}
```

Field descriptions:
- query_name: Domain name to query
- query_type: Query type (1=A, 28=AAAA, 2=NS, etc.)

**ICMP Module Configuration**:
```json
  "prober": "icmp",
  "timeout": 5,
  "icmp": {
    "packets": 3,
    "payload_size": 56,
    "pattern": 0
  }
}
```

Field descriptions:
- packets: Number of ICMP packets to send
- payload_size: Data payload size (bytes)
- pattern: Payload fill pattern (0-255)

#### Target Configuration (targets)

Target configuration references module definitions to execute specific probe tasks.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | Yes | Target name (used for metric labels) |
| target | string | Yes | Target hostname or IP address |
| port | number | Yes | Target port (ICMP can be 0) |
| module | string | Yes | Referenced module name |
| interval | number | No | Probe interval (seconds), defaults to global scrape_interval if not used |

#### Module Name Mapping

prober string corresponding module types:

| prober | Module Type | Description |
|--------|-------------|-------------|
 "http" | MODULE_HTTP | HTTP plain text connection |
 "https" | MODULE_HTTPS | HTTPS encrypted connection |
 "tcp" | MODULE_TCP | TCP connection test |
 "tcp_tls" | MODULE_TCP_TLS | TLS encrypted connection test |
 "dns" | MODULE_DNS | DNS resolution test |
 "icmp" | MODULE_ICMP | ICMP ping test |
 "ws" | MODULE_WS | WebSocket connection test |
 "wss" | MODULE_WSS | WebSocket Secure connection test |

## Web UI Configuration

After the device successfully connects to WiFi, it starts STA mode web configuration management interface.

### Access Configuration Interface

1. Confirm the device is connected to WiFi, get the device IP address
2. In browser, visit `http://<deviceIP>:80`

### Interface Functions

#### Device Information Panel

- **IP Address**: Device's current network IP address
- **WiFi SSID**: Currently connected WiFi network name
- **Uptime**: Device running time
- **Module Count**: Current configured probe module count
- **Target Count**: Current configured probe target count
- **Scrape Interval**: Global scrape interval setting
- **Metrics Port**: Metrics server port

#### Probe Status Table

| Column | Description |
|--------|-------------|
| Name | Target configuration name |
| Target | Probe target address |
| Port | Target port |
| Status | Probe result status (OK/FAIL) |
| Duration | Probe duration |
| Error | Error message (if any) |

#### Configuration Editor

- **JSON Editor**: Shows complete JSON of current configuration
- **Save**: Save configuration to SPIFFS file
- **Reload**: Reload configuration from SPIFFS (hot loading)
- **Hot Reload**: Reload configuration but don't validate formatting

### API Interfaces

#### Get Device Status

```bash
curl http://<deviceIP>:80/api/status
```

Response example:
```json
{
  "uptime_s": 3600,
  "modules": 3,
  "targets": 2,
  "scrape_interval_s": 30,
  "metrics_port": 9090,
  "results": [
    {
      "name": "httpbin_test",
      "target": "httpbin.org",
      "port": 80,
      "success": true,
      "duration_ms": 234,
      "error": ""
    },
    {
      "name": "dns_server",
      "target": "8.8.8.8",
      "port": 53,
      "success": false,
      "duration_ms": 0,
      "error": "DNS resolution failed"
    }
  ]
}
```

#### Get Configuration File

```bash
curl http://<deviceIP>:80/api/config
```

Returns complete JSON configuration content, can be used for backup.

#### Update Configuration

```bash
curl -X POST http://<deviceIP>:80/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "modules": {
      "http_2xx": {
        "prober": "http",
        "timeout": 10,
        "http": {
          "method": "GET",
          "valid_status_codes": [200]
        }
      }
    },
    "targets": [
      {
        "name": "test_target",
        "target": "httpbin.org",
        "port": 80,
        "module": "http_2xx",
        "interval": 60
      }
    ]
  }'
```

#### Hot Load Configuration

```bash
curl -X POST http://<deviceIP>:80/api/reload
```

#### Response Description

- **Status Code**: 200 means success, 4xx/5xx means failure
- **Error Message**: When failed, specific error reason is returned
- **Hot Loading**: After successful configuration takes effect immediately, no restart needed

## /probe Endpoint Usage

### Real-time Probe Execution

The system provides `/probe` endpoint for real-time probe execution, returning Prometheus format metrics.

#### Usage Method

```bash
curl "http://<deviceIP>:9090/probe?target=<hostname_or_IP>&module=<module_name>"
curl "http://<deviceIP>:9090/probe?target=<hostname_or_IP>&module=<module_name>&port=<port_number>"
```

#### Parameter Description

| Parameter | Required | Description |
|-----------|----------|-------------|
| target | Yes | Target hostname or IP address (supports any hostname, no pre-configuration required) |
| module | Yes | Module name |
| port | No | Target port number, optional. If 0 or omitted, module uses default port |

#### New Feature Description

**Important Update**: `/probe` endpoint now supports **arbitrary hostname and IP address** ad-hoc probing, no longer limited to pre-configured target names. This is an important improvement compatible with Prometheus blackbox_exporter.

**Port Parameter**: New optional `port` parameter supports specifying target port. If not specified, each module uses default port:
- HTTP/HTTPS: 80/443
- TCP: According to module configuration
- DNS: 53  
- ICMP: 0 (port not used)

#### Usage Examples

**HTTP Probe (ad-hoc targets)**:
```bash
# Probe any HTTP service
curl "http://192.168.1.100:9090/probe?target=www.baidu.com&module=http_2xx&port=80"

# Probe HTTPS service
curl "http://192.168.1.100:9090/probe?target=github.com&module=https_2xx&port=443"
```

**ICMP Probe (ad-hoc targets)**:
```bash
# Probe gateway connectivity
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=icmp_ping"

# Probe external host
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=icmp_ping"
```

**TCP Probe (ad-hoc targets)**:
```bash
# Probe port connectivity
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=tcp_connect&port=80"
curl "http://192.168.1.100:9090/probe?target=smtp.gmail.com&module=tcp_connect&port=587"
```

**DNS Probe (ad-hoc targets)**:
```bash
# Test DNS server
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=dns_resolve&port=53"
```

### Prometheus Scrape Configuration

Configure Prometheus scrape tasks using `/probe` endpoint:

```yaml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

scrape_configs:
  - job_name: 'blackbox_http'
    metrics_path: /probe
    params:
      module: [http_2xx]
    static_configs:
      - targets:
          - example.com:80    # Blackbox exporter style relabel configuration
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090  # Device IP

  - job_name: 'blackbox_icmp'
    metrics_path: /probe
    params:
      module: [icmp_ping]
    static_configs:
      - targets:
          - 192.168.1.1:0    # ICMP probe uses port 0
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090  # Device IP

  - job_name: 'blackbox_tcp'
    metrics_path: /probe
    params:
      module: [tcp_connect]
    static_configs:
      - targets:
          - smtp.gmail.com:587
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090  # Device IP
```

## Verification and Operation

### AP Mode (First Boot)

Normal startup log:
```
I (xxx) MAIN: ESP32 Blackbox Starting...
I (xxx) MAIN:   Target: esp32c6
I (xxx) MAIN:   Board: Seeed Studio XIAO ESP32C6
I (xxx) WIFI: AP mode started - connect to 'ESP32_Blackbox'
```

### STA Mode (WiFi Configured)

Normal startup log:
```
I (xxx) MAIN: ESP32 Blackbox Starting...
I (xxx) MAIN:   Target: esp32c6
I (xxx) WIFI: connected to 'YourWiFi'
I (xxx) WIFI: got ip:192.168.1.100
I (xxx) CFG_MGR: Config loaded: 3 modules, 2 targets
I (xxx) PROBE_MGR: Probe manager started
I (xxx) METRICS: Metrics server started on port 9090
I (xxx) WEB_SRV: STA web dashboard started on port 80
I (xxx) MAIN: All services started
```

### Get Device IP

Check in monitor logs:
```
I (xxx) WIFI: got ip:192.168.1.100
```

### Access Service Endpoints

```bash
 Web configuration interface
curl http://192.168.1.100:80/

 Device status API
curl http://192.168.1.100:80/api/status

 Prometheus metrics
curl http://192.168.1.100:9090/metrics

 Real-time probe execution
curl "http://192.168.1.100:9090/probe?target=www.baidu.com&module=http_2xx&port=80"
```

## Hardware Self-Test

ESP32-C6 defaults to running 7 hardware self-tests on startup to verify board functionality:

| # | Test Item | Verification Content |
|---|----------|---------------------|
| 1 | NVS Storage | Write→Read→Verify→Clear |
| 2 | WiFi Initialization | netif + WiFi driver + MAC address |
| 3 | WiFi Scan | STA mode scan surrounding APs |
| 4 | DNS Resolution | Resolve httpbin.org |
| 5 | HTTP Probe | Request httpbin.org |
| 6 | TCP Probe | Connect to 8.8.8.8:53 |
| 7 | Metrics Server | Port 9090 binding |

**Note**: Self-tests require WiFi network connection to complete DNS/HTTP/TCP tests. These tests will fail on first boot (AP mode), which is normal.

Adjust via `idf.py menuconfig` → ESP32 Blackbox Configuration → Run board self-test on startup.

## Prometheus Integration

### 1. Install Prometheus

Reference: https://prometheus.io/download/

### 2. Configure Prometheus

#### Method 1: Use /metrics endpoint (Traditional way)

Edit `prometheus.yml`:

```yaml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

scrape_configs:
  - job_name: 'esp32-blackbox'
    static_configs:
      - targets: ['192.168.1.100:9090']
```

### 3. Start Prometheus

```bash
./prometheus --config.file=prometheus.yml
```

### 4. Access Prometheus UI

Open browser and visit: http://localhost:9090

### 5. Query Metrics

In Expression box, enter queries:

```
 Traditional scraping method
probe_success
probe_duration_seconds
probe_status_code

 /probe endpoint method
probe_success{module="http_2xx"}
probe_duration_seconds{module="icmp_ping"}
probe_icmp_rtt_ms
```

### Grafana Integration (Optional)

1. Install Grafana: https://grafana.com/docs/grafana/latest/setup/
2. Add Prometheus data source
3. Create Dashboard

**Grafana Query Examples**:
```promql
# Probe success rate by module
sum(probe_success) by (module)

# Average response time by target
avg(probe_duration_seconds) by (target)

# ICMP response time distribution
histogram_quantile(0.95, probe_icmp_rtt_ms_bucket)

# Probe success rate trend
rate(probe_success[5m])
```

## Usage Tutorials

This section provides detailed practical usage cases to help users quickly get started with ESP32 Blackbox in various probing scenarios.

### 1. TCP Connectivity Test

**Purpose**: Test network device port accessibility, commonly used to check if gateway, router, or services are online.

**Example**: Test if gateway HTTP port is open

```bash
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=tcp_connect&port=80"
```

**Expected Result**:
```bash
# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="192.168.1.1", module="tcp_connect"} 0.012

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="192.168.1.1", module="tcp_connect"} 1

# HELP probe_status_code Status code or result code
# TYPE probe_status_code gauge
probe_status_code{target="192.168.1.1", module="tcp_connect"} 0
```

**Analysis**: `probe_success=1` indicates port is reachable, `probe_duration_seconds` shows connection time.

**Advanced Test**: Check SMTP service
```bash
curl "http://192.168.1.100:9090/probe?target=smtp.gmail.com&module=tcp_connect&port=587"
```

### 2. HTTP Service Availability Detection

**Purpose**: Verify if Web services are responding normally, detect HTTP status codes and response times.

**Example**: Check if Baidu homepage is accessible

```bash
curl "http://192.168.1.100:9090/probe?target=www.baidu.com&module=http_2xx&port=80"
```

**Expected Result**:
```bash
# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="www.baidu.com", module="http_2xx"} 0.156

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="www.baidu.com", module="http_2xx"} 1

# HELP probe_status_code Status code or result code
# TYPE probe_status_code gauge
probe_status_code{target="www.baidu.com", module="http_2xx"} 200
```

**Analysis**: `probe_status_code=200` indicates HTTP 200 OK response.

**HTTPS Test**: Check GitHub
```bash
curl "http://192.168.1.100:9090/probe?target=github.com&module=https_2xx&port=443"
```

### 3. DNS Resolution Verification

**Purpose**: Verify if DNS server is working properly, if domain name resolution is successful.

**Example**: Test public DNS server

```bash
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=dns_resolve&port=53"
```

**Expected Result**:
```bash
# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="8.8.8.8", module="dns_resolve"} 0.023

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="8.8.8.8", module="dns_resolve"} 1

# HELP probe_status_code Status code or result code
# TYPE probe_status_code gauge
probe_status_code{target="8.8.8.8", module="dns_resolve"} 0
```

**Analysis**: `probe_success=1` indicates DNS resolution successful.

**Custom domain test**:
```bash
curl "http://192.168.1.100:9090/probe?target=www.example.com&module=dns_resolve&port=53"
```

### 4. ICMP Ping Test

**Purpose**: Test network latency and connectivity, commonly used to monitor network quality and host availability.

**Example**: Test gateway latency

```bash
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=icmp_ping"
```

**Expected Result**:
```bash
# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="192.168.1.1", module="icmp_ping"} 0.025

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="192.168.1.1", module="icmp_ping"} 1

# HELP probe_icmp_rtt_ms Round-trip time for ICMP ping in milliseconds
# TYPE probe_icmp_rtt_ms gauge
probe_icmp_rtt_ms{target="192.168.1.1", module="icmp_ping"} 15.2

# HELP probe_icmp_packets_sent Number of ICMP packets sent
# TYPE probe_icmp_packets_sent gauge
probe_icmp_packets_sent{target="192.168.1.1", module="icmp_ping"} 3

# HELP probe_icmp_packets_received Number of ICMP packets received
# TYPE probe_icmp_packets_received gauge
probe_icmp_packets_received{target="192.168.1.1", module="icmp_ping"} 3
```

**Analysis**: `probe_icmp_rtt_ms` shows round-trip time in milliseconds, `probe_icmp_packets_received` shows successful packets received.

**External host test**:
```bash
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=icmp_ping"
```

### 5. Prometheus blackbox_exporter Integration

**Purpose**: Full integration with Prometheus monitoring system, automated monitoring and alerting.

#### Prometheus Configuration File

Create `prometheus-blackbox.yml`:

```yaml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

rule_files:
  - "blackbox_rules.yml"

scrape_configs:
# HTTP service monitoring
- job_name: 'blackbox_http'
    metrics_path: /probe
    params:
      module: [http_2xx]
    static_configs:
      - targets:
          - www.baidu.com:80
          - httpbin.org:80
          - github.com:443
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090  # ESP32 device IP
      - source_labels: [__param_target]
        regex: (.*):.*
        target_label: port
        replacement: $1

# TCP port monitoring
- job_name: 'blackbox_tcp'
    metrics_path: /probe
    params:
      module: [tcp_connect]
    static_configs:
      - targets:
          - 192.168.1.1:80    # Gateway HTTP
          - 192.168.1.1:22    # SSH
          - smtp.gmail.com:587 # SMTP
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090
      - source_labels: [__param_target]
        regex: (.*):.*
        target_label: port
        replacement: $1

# ICMP Ping monitoring
- job_name: 'blackbox_icmp'
    metrics_path: /probe
    params:
      module: [icmp_ping]
    static_configs:
      - targets:
          - 192.168.1.1:0    # Gateway
          - 8.8.8.8:0        # Google DNS
          - www.baidu.com:0   # Baidu
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090
      - source_labels: [__param_target]
        regex: (.*):.*
        target_label: port
        replacement: $1

# ESP32 device status monitoring
- job_name: 'esp32_blackbox'
    metrics_path: /metrics
    static_configs:
      - targets: ['192.168.1.100:9090']
```

#### Alert Rules

Create `blackbox_rules.yml`:

```yaml
groups:
  - name: blackbox_alerts
    rules:
# # HTTP service unavailable
- alert: HttpProbeFailed
    expr: probe_success{job="blackbox_http"} == 0
    for: 1m
    labels:
      severity: critical
    annotations:
      summary: "HTTP service unavailable: {{ $labels.instance }}"
      description: "HTTP service {{ $labels.instance }} continuously unavailable for 1 minute"

# # TCP port unavailable
- alert: TcpPortProbeFailed
    expr: probe_success{job="blackbox_tcp"} == 0
    for: 1m
    labels:
      severity: warning
    annotations:
      summary: "TCP port unavailable: {{ $labels.instance }}"
      description: "TCP port {{ $labels.instance }} continuously unavailable for 1 minute"

# # ICMP Ping timeout
- alert: IcmpProbeFailed
    expr: probe_success{job="blackbox_icmp"} == 0
    for: 2m
    labels:
      severity: warning
    annotations:
      summary: "ICMP Ping failed: {{ $labels.instance }}"
      description: "Host {{ $labels.instance }} ICMP Ping continuously failed for 2 minutes"

# # High response time
- alert: HighResponseTime
    expr: probe_duration_seconds{job="blackbox_http"} > 2
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "High response time: {{ $labels.instance }}"
      description: "HTTP service {{ $labels.instance }} response time over 2 seconds for 5 minutes"

# # High packet loss rate
- alert: HighPacketLoss
    expr: (probe_icmp_packets_sent - probe_icmp_packets_received) / probe_icmp_packets_sent > 0.2
    for: 2m
    labels:
      severity: warning
    annotations:
      summary: "High packet loss: {{ $labels.instance }}"
      description: "ICMP probe {{ $labels.instance }} packet loss over 20% for 2 minutes"
```

### 6. Scheduled Probing vs On-Demand Probing

#### Pre-configured Targets (Scheduled Probing)

Set up regularly executed probe tasks through configuration file:

```json
{
  "scrape_interval": 30,
  "metrics_port": 9090,
  "modules": {
    "http_2xx": {
      "prober": "http",
      "timeout": 10,
      "http": {
        "method": "GET",
        "valid_status_codes": [200]
      }
    },
    "icmp_ping": {
      "prober": "icmp",
      "timeout": 5,
      "icmp": {
        "packets": 3,
        "payload_size": 56
      }
    }
  },
  "targets": [
    {
      "name": "baidu_http",
      "target": "www.baidu.com",
      "port": 80,
      "module": "http_2xx",
      "interval": 30
    },
    {
      "name": "gateway_icmp",
      "target": "192.168.1.1",
      "port": 0,
      "module": "icmp_ping",
      "interval": 60
    }
  ]
}
```

**Features**:
- System automatically executes on schedule
- Configuration persistent storage
- Supports different interval times
- Suitable for long-term monitoring

#### Temporary Probing (On-Demand)

Execute immediate probing through `/probe` endpoint:

```bash
# Temporary check service status
curl "http://192.168.1.100:9090/probe?target=www.example.com&module=http_2xx&port=80"

# Quick diagnosis during failure
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=icmp_ping"

# Test newly deployed service
curl "http://192.168.1.100:9090/probe?target=new-service.local&module=http_2xx&port=8080"
```

**Features**:
- Immediate execution, no waiting for scheduling
- Supports any target
- Temporary use, not saving configuration
- Suitable for troubleshooting and testing

### Summary

This tutorial covers various usage scenarios for ESP32 Blackbox:

- **Basic connectivity testing**: TCP, ICMP, DNS
- **Service availability detection**: HTTP/HTTPS service status
- **System integration**: Prometheus monitoring configuration and alert rules
- **Usage patterns**: Pre-configured scheduled probing vs on-demand temporary probing
- **Command line tools**: curl batch testing and performance testing
- **Browser access**: Web UI usage and debugging techniques

Through these practical cases, users can choose appropriate probing methods based on their needs, quickly diagnose network issues, and establish complete monitoring systems.

## Troubleshooting

### Cannot Find AP Hotspot

**Symptoms**: Phone/computer cannot find `ESP32_Blackbox` WiFi hotspot.

**Reason 1: board_test WiFi initialization conflict (Fixed)**

**Fix**: `board_test.c` added `esp_wifi_deinit()` complete deinitialization after WiFi scan test; `wifi_manager_init()` added fault tolerance for `esp_netif_init()` and `esp_wifi_init()` `ESP_ERR_INVALID_STATE`.

**Reason 2: Device not fully started**

1. Confirm device is powered on and fully started (wait 5 seconds)
2. Check serial logs to confirm AP mode has started
3. Get closer to device, WiFi signal range is limited

**Reason 3: NVS data corruption**

```bash
idf.py -p COM6 erase-flash
idf.py -p COM6 flash
```

### App Partition Overflow

**Symptoms**: Build error `app partition is too small for binary`.

**Cause**: ESP32-C6 firmware size exceeds default partition table's 1MB app limit.

**Fix**: Project uses custom partition table `partitions.csv`, expanding app partition to 1.875MB. This configuration automatically takes effect in `sdkconfig.defaults.esp32c6`:

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

### Toolchain Version Mismatch

**Symptoms**: Build error `Tool doesn't match supported version from list ['esp-15.2.0_20251204']`.

**Cause**: ESP-IDF v6.0 requires `esp-15.2.0_20251204` toolchain, old `esp-14.2.0` is not compatible.

**Fix**: Run ESP-IDF installer to update toolchain:

```bash
cd $IDF_PATH
./install.sh esp32c6    # Install tools needed for C6
```

Or manually check if `esp-15.2.0_20251204` directory exists under `C:\Espressif\tools\riscv32-esp-elf\`.

### WiFi Connection Failed

1. Check SSID and password in NVS are correct
2. Confirm WiFi signal strength
3. Check if special characters need escaping
4. Erase NVS and reconfigure: `idf.py erase-flash`

### Configuration File Issues

**Symptoms**: Configuration file corrupted or format error

**Solutions**:
1. Use "Reload" button in Web UI to reload from SPIFFS
2. Check JSON format is correct
3. Delete `/spiffs/blackbox.json` file to let system use factory default configuration

**File check**:
```bash
 View SPIFFS status via serial monitor
idf.py -p COM6 monitor

 Check configuration logs in monitor
I (xxx) CFG_MGR: SPIFFS: Total=XXXX bytes, Used=YYYY bytes
I (xxx) CFG_MGR: JSON parsing complete: X modules, Y targets
```

### All Probes Failed

1. Confirm target servers are reachable
2. Check firewall settings
3. Increase timeout_ms value
4. Check if module references in configuration are correct

### Metrics Service Unresponsive

1. Confirm device has obtained IP
2. Check metrics_port configuration
3. Confirm port is not in use

### /probe Endpoint Errors

1. Confirm target and module names exist
2. Check parameter format is correct
3. Check if configuration in Web UI is correct

**Common errors**:
- `target not found`: Target name doesn't exist (note: now supports arbitrary hostname/IP)
- `module not found`: Module name doesn't exist
- `probe timeout`: Probe timeout, try increasing timeout
- `invalid target`: Target configuration invalid

### Web UI Access Issues

1. Confirm device IP address is correct
2. Check if port 80 is in use
3. Confirm device is connected to STA mode

### Compilation Errors

1. Ensure ESP-IDF v6.0 is fully installed
2. Target is set: `idf.py set-target esp32c6`
3. Update submodules: `git submodule update --init --recursive`
4. Clean and rebuild: `idf.py fullclean && idf.py build`
5. Confirm `components/json/` directory exists (v6.0 local cJSON component)

### TLS Handshake Failed

1. Confirm target port supports TLS (usually 443)
2. If certificate validation is needed, ensure device time is synchronized (NTP)
3. Try `verify_ssl = false` to exclude certificate issues
4. Check mbedTLS error codes in logs (format `-0xXXXX`), common errors:
   - `-0x2700`: Certificate validation failed
   - `-0x7200`: SSL handshake timeout
   - `-0x0050`: Network send failed

### XIAO ESP32C6 Flash Size

Some batches of XIAO ESP32C6 come with 8MB Flash (not 4MB). If esptool reports Flash size mismatch during flashing, modify `sdkconfig.defaults.esp32c6`:

```
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
```

Then execute `python build.py esp32c6 clean` to rebuild.

### Configuration Hot Loading Issues

If configuration doesn't take effect after update:

1. Check if JSON format of configuration file is correct
2. Confirm module references exist
3. Check if "Reload" button in Web UI executed successfully
4. Restart device for configuration to fully take effect

**Debug information**:
```bash
 Check configuration version changes via serial
idf.py -p COM6 monitor

I (xxx) CFG_MGR: Hot loading configuration...
I (xxx) CFG_MGR: JSON parsing complete: X modules, Y targets
I (xxx) CFG_MGR: Configuration hot loaded: version=Z
```