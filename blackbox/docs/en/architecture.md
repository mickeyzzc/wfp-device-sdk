[English](../en/architecture.md) | **中文**

# ESP32 Blackbox Architecture Design

## System Overview

ESP32 Blackbox is an embedded network probing system based on ESP32-C3/C6, featuring modular design with initial boot AP configuration mode. The system centers on a blackbox exporter compatible architecture, implementing flexible probe module systems and SPIFFS configuration management.

```
┌─────────────────────────────────────────────────────┐
                Application Layer                    │
│  ┌─────────────┐  ┌───────────────────────────┐    │
│  │    main.c    │  │   Unified Web Server     │    │
│  └─────────────┘  │  (STA Dashboard + AP UI) │    │
├─────────────────────────────────────────────────────┤
                Service Layer                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────┐  │
│  │ WiFi Manager │  │ Probe Manager│  │  Metrics  │  │
│  │ (AP/STA)     │  │              │  │  Server   │  │
│  └──────────────┘  └──────────────┘  │ (Port 9090)│
└─────────────────────────────────────────────────────┤
                Probe Layer                          │
  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌───┐  │
    │  HTTP  │ │TCP/TLS │ │  DNS   │ │  ICMP   │ │WS │  │
  └────────┘ └────────┘ └────────┘ └────────┘ │WSS│  │
├─────────────────────────────────────────────────────┤
          Ad-Hoc Probe Support (/probe)             │
           ┌────────────────────────────┐          │
           │  Dynamic on-demand probe    │          │
           │  /probe?target=X&          │          │
           │     module=Y&            │          │
           │        port=P              │          │
           └────────────────────────────┘          │
├─────────────────────────────────────────────────────┤
                Config Layer                         │
  ┌─────────────────────────────────────────────┐    │
│       Config Manager (SPIFFS + JSON)        │    │
│  • Module system (modules[])                  │    │
│  • Target definitions (targets[])              │    │
│  • Hot loading support                        │    │
└─────────────────────────────────────────────┘    │
```

## Core Architecture Features

### 1. Blackbox Exporter Compatible Design

The system is fully compatible with blackbox_exporter's probing model, following Prometheus ecosystem standards:

- **Label system**: `target="target_name"`, `module="module_name"` (not `target/port/type`)
- **Metric format**: Standard Prometheus text format, easy integration
- **/probe endpoint**: Supports real-time execution, on-demand probing and blackbox_exporter style scraping configurations
- **On-demand probing**: Supports arbitrary hostnames/IP addresses with optional port parameters

### 2. Modular Probe System

Protocol probing and configuration separation design:

- **Module definitions**: Configuration parameters for each protocol defined in `modules[]`
- **Target references**: Targets reference modules by `module_name` for configuration reuse
- **Type distribution**: probe_manager executes corresponding probing based on target's referenced module type

### 3. SPIFFS Configuration Layer

Flexible management of JSON configuration files:

- **Persistent storage**: SPIFFS filesystem saves complete configuration
- **Hot loading**: Supports POST /api/config and POST /reload for dynamic updates
- **Factory defaults**: Built-in default configuration automatically used when configuration file is missing

## Core Module Details

### 1. WiFi Manager (`wifi_manager.c/h`)

Manages WiFi connections, supporting AP and STA dual modes.

**Functions:**
- Initialize WiFi, automatically check if WiFi credentials are stored in NVS
- AP mode: Start hotspot for user WiFi configuration (SSID: `ESP32_Blackbox`, password: `12345678`)
- STA mode: Connect to configured WiFi network
- Event-driven connection management with auto-reconnect mechanism
- Connection status query

**Key Design:**
- Use FreeRTOS EventGroup to synchronize connection status
- Automatically enter AP mode on first boot with no credentials
- Store WiFi SSID and password in NVS, auto-connect after reboot
- Support configurable maximum retry count (default 5 times)

### 2. Config Manager (`config_manager.c/h`)

Unified system configuration management, implementing modular probing architecture.

**Core Data Structures:**
```c
// Module definition: Collection of protocol configurations
typedef struct {
    char name[32];                              // Module name
    probe_module_config_t config;               // Module configuration
} probe_module_t;

// Target definition: Specific probe task referencing a module
typedef struct {
    char name[64];                              // Target name  
    char target[256];                          // Target address
    uint16_t port;                             // Target port
    uint32_t interval_ms;                      // Probe interval
    char module_name[32];                       // Referenced module name
} probe_target_t;

// Main configuration structure
typedef struct {
    probe_module_t *modules;                    // Module array
    uint8_t module_count;                       // Module count
    probe_target_t *targets;                    // Target array
    uint8_t target_count;                       // Target count
    uint32_t scrape_interval_ms;               // Global scrape interval
    uint16_t metrics_port;                     // Metrics port
} blackbox_config_t;
```

**Functions:**
- Mount SPIFFS filesystem and read/write JSON configuration
- Configuration validation for modules and targets
- Hot loading support (s_config_version counter)
- Generate factory default configuration
- NVS WiFi credential storage (independent)

**Configuration File Format:**
```json
{
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
     "name": "httpbin_http",
     "target": "httpbin.org",
     "port": 80,
     "module": "http_2xx"
   },
   {
     "name": "httpbin_dns",
     "target": "8.8.8.8",
     "port": 53,
     "module": "dns_resolve"
   }
 }
}
```

### 3. Probe Manager (`probe_manager.c/h`)

Probe task scheduler, implementing modular protocol probing.

**Responsibilities:**
- Manage probe task lifecycle
- Execute corresponding protocol based on target's referenced module type
- Poll and execute all configured probes
- Store probe results for metrics server use

**Task Flow:**
```
while (1) {
    for (each probe target) {
        const probe_module_t *module = config_get_module_by_name(target.module_name)
        Execute probe function corresponding to module->config.type
        Save results
    }
    Wait for interval_ms
}
```

### 4. Probe Modules

Protocol probing implementations, each module independently maintains configuration parameters.

| Module | Type | File | Supported Features |
--------|------|------|-------------------|
 HTTP | `MODULE_HTTP` | `probe_http.c` | HTTP GET/POST, status code validation |
 HTTPS | `MODULE_HTTPS` | `probe_http.c` | TLS encryption, certificate validation |
 TCP | `MODULE_TCP` | `probe_tcp.c` | TCP connection test |
 TCP+TLS | `MODULE_TCP_TLS` | `probe_tcp.c` | TLS handshake timing |
 DNS | `MODULE_DNS` | `probe_dns.c` | DNS resolution test |
 ICMP Ping | `MODULE_ICMP` | `probe_icmp.c` | Ping test, RTT measurement |
 WebSocket | `MODULE_WS` | `probe_ws.c` | WebSocket connection test |
 WebSocket Secure | `MODULE_WSS` | `probe_ws.c` | TLS + WebSocket handshake timing |

**Probe Result Structure:**
```c
typedef struct {
    bool success;               // Whether successful
    uint32_t duration_ms;       // Total duration
    int status_code;            // Status code/result code
    char error_msg[128];        // Error message
    union {
        struct { /* HTTP details: connect_time, tls_time, ttfb, status */ } http;
        struct { /* TCP details: connect_time, tls_time */ } tcp;
        struct { /* DNS details: resolve_time, resolved_ip */ } dns;
        struct { /* ICMP details: packets_sent, packets_received, rtt_ms */ } icmp;
        struct { /* WebSocket details: connect_time, tls_time, handshake_time */ } ws;
    } details;
} probe_result_t;
```

### 5. Unified Web Server (Supports AP + STA + On-Demand Probing)

Unified web server providing services in three modes:

**AP Mode (WiFi Configuration Portal):**
- Port 80, dark theme configuration page
- WiFi scanning and credential saving
- `/`, `/scan`, `/save` routes

**On-Demand Probe Mode (Dynamic Execution):**
- `/probe?target=X&module=Y&port=P` real-time probing
- Supports arbitrary hostnames and IP addresses
- Returns Prometheus format immediate results

**STA Mode (Configuration Management Dashboard):**
- Device status and configuration editing interface
- `/api/status`, `/api/config`, `/api/reload` APIs
- Real-time probe status display

**Route Distribution Logic:**
```c
if (wifi_manager_is_connected()) {
    // STA mode - configuration management and on-demand probing
    web_server_handle_probe_request();    // /probe endpoint
    web_server_start_config_api();       // /api/* endpoints
} else {
    // AP mode - WiFi configuration
    wifi_config_server_start();           // /scan, /save endpoints
}
```

## System Startup Flow

```
main()
├─ print_board_info()            // Print board info (C3/C6)
│
├─ board_test_run()              // Hardware self-test (C6 default on, C3 default off)
│
├─ config_manager_init()         // SPIFFS mount → config loading
│   ├─ mount_spiffs()            // Mount SPIFFS filesystem
│   ├─ config_load_from_spiffs() // Read /spiffs/blackbox.json
│   ├─ config_validate()        // Validate configuration
│   └─ Use factory defaults if config fails
│
├─ wifi_manager_init()
│   ├─ Check if WiFi credentials in NVS
│   │   ├─ No credentials → Start AP mode → wifi_config_server_start()
│   │   │                    // Web config portal (port 80)
│   │   └─ Wait for user configuration
│   │
│   └─ Has credentials → Start STA mode → Wait for WiFi connection
│       └─ Connection successful → web_server_start()
│                       // STA mode dashboard (port 80)
│
├─ probe_manager_init()
│   └─ probe_manager_start()     // Start probe tasks
│
└─ metrics_server_start()        // Start HTTP server (port 9090)
```

## Thread Model

System uses FreeRTOS multi-task architecture, each task has clear responsibilities:

| Task Name | Priority | Stack Size | Function |
-----------|----------|------------|----------|
 probe_task | 5 | 16KB | Execute network probing (including TLS) |
 metrics_server | 5 | 8KB | Prometheus HTTP server (port 9090) |
 web_server | 5 | 8KB | STA mode config dashboard (port 80) |
 wifi_config_server | 5 | 4KB | AP mode web server (port 80) |
 WiFi events | System | - | WiFi event handling |

## Network Service Architecture

### HTTP Service Endpoints

#### /metrics Endpoint (Prometheus Scraping)
```
GET /metrics
HTTP/1.1 200 OK
Content-Type: text/plain; version=0.0.4

# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="google_http", module="http_2xx"} 0.234
probe_duration_seconds{target="google_dns", module="dns_resolve"} 0.089

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="google_http", module="http_2xx"} 1
probe_success{target="google_dns", module="dns_resolve"} 1

# HELP probe_tls_handshake_seconds Duration of TLS handshake in seconds
# TYPE probe_tls_handshake_seconds gauge
probe_tls_handshake_seconds{target="google_https", module="https"} 0.128

# HELP probe_icmp_rtt_ms Round-trip time for ICMP ping in milliseconds
# TYPE probe_icmp_rtt_ms gauge
probe_icmp_rtt_ms{target="ping_host", module="icmp_ping"} 15.2
```

#### /probe Endpoint (Real-time Execution)
```
GET /probe?target=httpbin.org&module=http_2xx
HTTP/1.1 200 OK
Content-Type: text/plain; version=0.0.4

# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="httpbin.org", module="http_2xx"} 0.234

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="httpbin.org", module="http_2xx"} 1

# HELP probe_status_code Status code or result code
# TYPE probe_status_code gauge
probe_status_code{target="httpbin.org", module="http_2xx"} 200
```

### Configuration Management API

#### STA Mode Web Server (Port 80)
```
GET /api/status          // Device status JSON
POST /api/config        // Update configuration JSON
GET /api/config         // Get current configuration JSON
POST /api/reload         // Hot load configuration
GET /                   // Configuration management dashboard page
```

#### AP Mode Configuration Portal (Port 80)
```
GET /                   // WiFi configuration page
GET /scan               // WiFi scan results JSON
POST /save              // Save WiFi credentials
```

## Data Flow Architecture

```
SPIFFS Config File] ──→ Config Manager ──→ Module System
                                │
    ┌─────────────────┐               │
    │ Factory Defaults│               │
    └─────────────────┘               │
                                │
─→ Probe Manager ←─ Target References  │
        │                              │
        └─→ Protocol Probe Modules      │
                    │                  │
                    └─→ Result Storage    │
                                │
─→ Metrics Server ←─┐            │
                   │            │
─→ Prometheus Scraping ┼─→ /probe Endpoint │
                   │            │
─→ Web UI Config   ┼─→ /api/config    │
```

## Configuration Hot Loading Mechanism

### Version Counter
```c
// Configuration version management
static uint8_t s_config_version = 0;

// Hot loading function
esp_err_t config_reload(void) {
    // Reload configuration from SPIFFS
    // Validate configuration validity
    s_config_version++;
    return ESP_OK;
}
```

### Configuration Update Flow
```
POST /api/config (Web UI)

─ Read JSON request body
─ Call config_update_targets()
   ├─ Parse JSON into memory
─ Validate configuration
─ Save to SPIFFS
─ Update s_config_version++

POST /reload (API or direct call)
─ Call config_reload()
   ├─ Reread SPIFFS
─ Validate and activate
```

## Performance and Resource Considerations

### Memory Layout

| Component | Estimated Memory |
-----------|-----------------|
 WiFi Stack | ~40KB |
 LwIP | ~20KB |
 Probe Task | ~16KB stack (with TLS) |
 Metrics Server | ~8KB stack |
 Web Server Task | ~8KB stack |
 Config Manager | ~4KB stack |
 TLS (mbedTLS v4 / PSA) | ~25KB |
 App Partition | ~960KB (94% used) |

### Probe Performance

- **Minimum probe interval**: 5000ms (prevents watchdog timeout)
- **HTTP/HTTPS**: 10-30 second timeout
- **TCP**: 5-10 second timeout
- **TCP+TLS/WSS**: 10-15 second timeout (TLS handshake time)
- **DNS**: 5 second timeout
- **ICMP**: 5 second timeout

### Concurrency Model

Uses serial probing model to avoid resource competition:

- probe_task single-threadedly polls all targets
- No concurrent probing to ensure stability
- Interval execution prevents memory and CPU overload

## Extension Plans

### Completed
- [x] SPIFFS JSON configuration system
- [x] Modular probing architecture
- [x] ICMP ping probing
- [x] Hot loading configuration mechanism
- [x] Unified web server (AP + STA + on-demand probing)
- [x] /probe endpoint real-time execution
- [x] Blackbox exporter compatible label system
- [x] On-demand probing feature - supports arbitrary hostname/IP and port parameters

### On-Demand Probe Architecture
- Supports dynamic target configuration and real-time execution
- Module system supports dynamic protocol parameter selection
- Prometheus compatible format returns results
- No configuration file modification needed for network testing

### Long-term Planning
- [ ] Multi-device cluster management
- [ ] OTA firmware updates
- [ ] Edge computing integration
- [ ] Cloud configuration synchronization