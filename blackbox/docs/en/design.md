[English](../en/design.md) | **中文**

# ESP32 Blackbox Design Document

## Design Goals

1. **Blackbox Exporter Compatible**: Fully compatible with blackbox_exporter label system and scraping mode
2. **Modular Architecture**: Separate protocol probing from configuration, supporting flexible parameter combinations
3. **Hot Configuration Loading**: Update probe configuration without restart
4. **Zero-Config Startup**: First boot AP mode, web page WiFi configuration
5. **Resource Optimization**: Optimized design for ESP32-C3/C6 400-512KB SRAM

## Technology Selection

### ESP-IDF v6.0

Key reasons for choosing ESP-IDF v6.0:
- mbedTLS v4 + PSA Crypto API, more modern cryptographic framework
- Removed old `entropy.h`, `ctr_drbg.h`, simplified TLS configuration
- Better WiFi AP/STA switching support
- Native NVS non-volatile storage support
- Better RISC-V (ESP32-C3/C6) support

### HTTP Framework Selection

**Original Plan**: LwIP raw API

**Final Plan**: esp_http_server (LWIP + Standard HTTP)

**Selection Reasons**:
1. **API Simple**: esp_http_server provides standard HTTP server interface
2. **Feature Complete**: Automatically handles HTTP protocol details
3. **Stable and Reliable**: Fully tested by ESP-IDF
4. **Resource Friendly**: More memory efficient than raw API

### Configuration Storage Selection

**Candidate Options**:
- NVS key-value pair storage
- File system storage
- SPIFFS + JSON configuration files

**Selected SPIFFS + JSON**:
1. **Configuration Complexity**: JSON supports complex data structures (modules, target arrays)
2. **Readability**: Human-readable configuration files
3. **Toolchain**: Rich JSON processing libraries (cJSON)
4. **Extensibility**: Easy to add new configuration fields
5. **Debug Friendly**: Direct file content viewing

### Target Hardware Support

| Project | ESP32-C3 SuperMini | Seeed XIAO ESP32C6 |
|---------|-------------------|-------------------|
| Chip | ESP32-C3 (QFN32) | ESP32-C6FH4 (QFN32) |
| Architecture | RISC-V single-core 160MHz | RISC-V dual-core (HP 160MHz + LP 20MHz) |
| Flash | 4MB (embedded) | 4MB (embedded) |
| SRAM | 400KB | 512KB |
| WiFi | 802.11 b/g/n | 802.11ax (WiFi 6) |
| BLE | Bluetooth 5 LE | Bluetooth 5.3 LE |
| Others | - | Zigbee, Thread (IEEE 802.15.4) |
| Partition Table | Default (1MB app) | Custom (1.875MB app) |

### Protocol Stack Selection

| Protocol | Implementation File | Core Dependencies | Features |
|----------|-------------------|------------------|----------|
 HTTP/HTTPS | `probe_http.c` | esp_http_client | Supports custom methods, status code validation |
 TCP | `probe_tcp.c` | lwip/sockets | Basic TCP connection test |
 TCP+TLS | `probe_tcp.c` | lwip/sockets + mbedtls v4 | TLS handshake timing |
 DNS | `probe_dns.c` | lwip/netdb | DNS resolution response time |
 ICMP Ping | `probe_icmp.c` | lwip/sockets + lwip/icmp | Raw sockets, RTT measurement |
 WebSocket | `probe_ws.c` | lwip/sockets | Basic WebSocket connection |
 WebSocket Secure | `probe_ws.c` | lwip/sockets + mbedtls v4 | TLS + WebSocket handshake timing |

## Core Design Decisions

### 1. Modular Probe Architecture

**Design Problem**: How to support multiple probing protocols with flexible configuration?

**Solution**: Module system design

**Key Design Points**:
- **Separate Configuration**: Protocol parameter configuration separated from specific probe targets
- **Reference Mechanism**: Targets reference module configurations by `module_name`
- **Type Distribution**: probe_manager distributes to corresponding implementations based on module type

**Implementation Example**:
```c
// Module definition - tells system how to probe HTTP
static probe_module_t s_modules[] = {
    {
        .name = "http_2xx",
        .config = {
            .type = MODULE_HTTP,
            .timeout_ms = 10000,
            .config.http = {
                .method = "GET",
                .valid_status_codes = {200},
                .no_follow_redirects = false
            }
        }
    }
};

// Target definition - uses module
static probe_target_t s_targets[] = {
    {
        .name = "google_homepage",
        .target = "google.com",
        .port = 80,
        .interval_ms = 30000,
        .module_name = "http_2xx"  // References the module config above
    }
};
```

### 2. SPIFFS Configuration Hot Loading

**Design Problem**: How to achieve runtime configuration updates?

**Solution**: Version counter + SPIFFS reload

**Core Mechanisms**:
- **Configuration Version**: `static uint8_t s_config_version`
- **Atomic Update**: After configuration validation, s_config_version++
- **Event Driven**: Each probe loop checks for version changes
- **Synchronous Activation**: Version changes automatically trigger configuration reload

**Workflow**:
```c
// Probe task main loop
void probe_manager_loop(void) {
    // Check for configuration version changes
    if (config_get_version() != s_config_version) {
        ESP_LOGI(TAG, "Config version changed, reloading...");
        probe_manager_reload_targets();
        s_config_version = config_get_version();
    }
    // Execute probes
    for (int i = 0; i < target_count; i++) {
        probe_target_t *target = &targets[i];
        const probe_module_t *module = config_get_module_by_name(target->module_name);
        execute_probe(module, target);
    }
    // Wait for interval
    vTaskDelay(pdMS_TO_TICKS(interval_ms));
}
```

### 4. Unified Web Server Architecture

**Design Problem**: How to simultaneously support AP mode configuration and on-demand probing?

**Solution**: Single server supporting both configuration management and on-demand probing

**Dual Mode Design**:
- **AP Mode**: WiFi credential configuration (`/`, `/scan`, `/save`)
- **On-Demand Probing**: `/probe?target=X&module=Y&port=P` endpoint
- **STA Mode**: Configuration management dashboard (`/api/status`, `/api/config`, `/reload`)

**Routing Logic**:
```c
// Web server route distribution
if (wifi_manager_is_connected()) {
    // STA mode - configuration management and on-demand probing
    web_server_handle_probe_request();     // /probe endpoint
    web_server_start_config_api();        // /api/* endpoints
} else {
    // AP mode - WiFi configuration
    wifi_config_server_start();           // /scan, /save endpoints
}
```

### 5. Raw ICMP Implementation

**Design Problem**: How to implement ICMP ping with ESP-IDF v6.0?

**Solution**: lwIP raw sockets

**Technical Points**:
- **SOCK_RAW + IPPROTO_ICMP**: Use lwIP raw sockets to send ICMP
- **Checksum Calculation**: Manually implement ICMP checksum algorithm
- **Packet Matching**: Use task handle as ICMP ID to ensure packet uniqueness

**Implementation Advantages**:
- **lwIP Compatible**: Good integration with existing network stack
- **Low Latency**: Direct network access, no additional protocol layer
- **Reliable**: Complete Echo Request/Reply implementation

## Performance Optimization Design

### Memory Management

| Component | Memory Usage | Optimization Strategy |
|-----------|--------------|----------------------|
 Probe Task | 16KB stack | Avoid dynamic allocation, use stack variables |
 Config System | Static arrays | Fixed maximum limits, avoid OOM |
 TLS Stack | 8KB | Use PSA Crypto, reduce entropy overhead |
 JSON Processing | Reused buffers | Limit config file size (64KB) |
 Network Buffer | lwIP default | Reasonable SO_RCVTIMEO settings |

### Probe Scheduling Optimization

**Serial Execution Strategy**:
- **Advantages**: Simple implementation, predictable resource usage
- **Advantages**: Avoid multi-threaded race conditions
- **Disadvantages**: Cannot probe different targets in parallel
- **Balance**: Reasonable intervals meet most usage scenarios

**Interval Settings**:
```c
// Minimum interval protection (prevents watchdog timeout)
#define MIN_PROBE_INTERVAL_MS    5000

// Automatic interval adjustment (if target doesn't specify interval)
if (target->interval_ms == 0) {
    target->interval_ms = s_config.scrape_interval_ms;
}
```

### Error Handling Strategy

**Silent Failure Mode**:
- Individual probe failure doesn't affect other probe tasks
- Error information recorded in result.error_msg
- Expose failure status through metrics
- System remains stable overall

**Error Classification Handling**:
```c
// Timeout error
if (result.success == false && strstr(result.error_msg, "timeout")) {
    // May be network issue, retry next time
}

// Connection error
if (result.success == false && strstr(result.error_msg, "connection")) {
    // May be service unavailable, mark as failed
}

// Protocol error
if (result.success == false && strstr(result.error_msg, "protocol")) {
    // Configuration issue, need to check configuration
}
```

## Security Design

### WiFi Security
- **AP Mode**: WPA2-PSK encryption, default password `12345678` (LAN use)
- **STA Mode**: User-configured WPA2/WPA3 encrypted networks
- **NVS Storage**: WiFi credentials encrypted storage, secure transmission

### Configuration File Security
- **File Permissions**: SPIFFS config files system read/write only
- **Input Validation**: Strict JSON format and data range validation
- **Prevent Overflow**: Fixed-size buffers, use strncpy/snprintf

### Network Service Security
- **AP Mode**: Only runs when clients connected, auto-closes after completion
- **STA Mode**: Configuration management limited to LAN access
- **Input Sanitization**: All user input undergoes length and format checking

### TLS Security

**mbedTLS v4 + PSA Crypto**:
- Automatic key management, no manual random number handling
- SNI (Server Name Indication) support
- Optional certificate validation (via verify_ssl config)
- Phased timing for performance analysis

## Extensibility Design

### Adding New Probe Protocols

**Step 1**: Update configuration header file
```c
// config_manager.h
typedef enum {
    MODULE_HTTP,      // Existing
    MODULE_TCP,       // Existing
    MODULE_DNS,       // Existing
    MODULE_ICMP,      // Existing
    MODULE_NEW_PROTO, // New
} probe_module_type_t;
```

**Step 2**: Implement probe function
```c
// probe_new_proto.c
probe_result_t probe_new_proto_execute(const probe_target_t *target,
                                      const probe_module_config_t *module_config) {
    // Implement new protocol probing logic
}
```

**Step 3**: Update module configuration
```c
// config_manager.c
typedef struct {
    uint32_t timeout_ms;
    // New protocol specific configuration
    new_proto_config_t new_proto;
} module_config_union_t;
```

**Step 4**: Register to configuration system
```c
// config_manager.c - Factory default configuration
static const probe_module_t s_factory_modules[] = {
    {
        .name = "new_proto_module",
        .config = {
            .type = MODULE_NEW_PROTO,
            .timeout_ms = 10000,
            .config.new_proto = {/* Default configuration */}
        }
    }
};
```

### Configuration System Extension

**Add New Configuration Fields**:
```json
{
  "modules": {
   "existing_module": {
      "prober": "http",
      "timeout": 10,
      "http": {
        "method": "GET",
        "new_field": "value"  // New field
      }
    }
  },
  "targets": [...]
}
```

**Backward Compatibility**:
- New fields use default values, old configs still work normally
- Removed fields automatically use defaults during upgrade
- Version control ensures smooth configuration migration

## Monitoring Metrics Design

### Metrics Classification

| Metric Name | Type | Labels | Purpose |
|-------------|------|--------|---------|
 probe_duration_seconds | Gauge | target, module | Total probe duration |
 probe_success | Gauge | target, module | Whether probe succeeded |
 probe_status_code | Gauge | target, module | HTTP status code or result code |
 probe_tls_handshake_seconds | Gauge | target, module | TLS handshake duration (applicable only) |
 probe_icmp_rtt_ms | Gauge | target, module | ICMP round-trip time (ICMP only) |
 probe_icmp_packets_sent | Gauge | target, module | ICMP packets sent (ICMP only) |
 probe_icmp_packets_received | Gauge | target, module | ICMP packets received (ICMP only) |

### Label Design Principles

1. **Target Identification**: `target="target_name"` provides business logic identification
2. **Module Identification**: `module="module_name"` provides technical type identification  
3. **Avoid Pollution**: Don't use generic labels like `host`, `instance`
4. **Field Stability**: Label names and meanings maintain backward compatibility

### Metrics Examples

**HTTP Probe Metrics**:
```
# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="google_http", module="http_2xx"} 0.234

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="google_http", module="http_2xx"} 1

# HELP probe_status_code Status code or result code
# TYPE probe_status_code gauge
probe_status_code{target="google_http", module="http_2xx"} 200
```

**ICMP Probe Metrics**:
```
# HELP probe_duration_seconds Duration of the probe in seconds
# TYPE probe_duration_seconds gauge
probe_duration_seconds{target="ping_host", module="icmp_ping"} 0.025

# HELP probe_success Whether the probe succeeded
# TYPE probe_success gauge
probe_success{target="ping_host", module="icmp_ping"} 1

# HELP probe_icmp_rtt_ms Round-trip time for ICMP ping in milliseconds
# TYPE probe_icmp_rtt_ms gauge
probe_icmp_rtt_ms{target="ping_host", module="icmp_ping"} 15.2

# HELP probe_icmp_packets_sent Number of ICMP packets sent
# TYPE probe_icmp_packets_sent gauge
probe_icmp_packets_sent{target="ping_host", module="icmp_ping"} 3

# HELP probe_icmp_packets_received Number of ICMP packets received
# TYPE probe_icmp_packets_received gauge
probe_icmp_packets_received{target="ping_host", module="icmp_ping"} 3
```

## Future Plans

### Implemented Features
- [x] Modular probe architecture (modules[] + targets[])
- [x] SPIFFS JSON configuration system
- [x] Hot loading configuration mechanism
- [x] ICMP ping probing implementation
- [x] Unified web server architecture
- [x] Blackbox exporter compatible label system
- [x] /probe endpoint real-time execution

### Short-term Plan (1-2 months)
- [ ] Probe result history storage and trend analysis
- [ ] Probe failure intelligent retry mechanism
- [ ] Probe health scoring algorithm
- [ ] WebSocket real-time status push
- [ ] Configuration file version control and rollback
- [ ] Probe task scheduling optimization
- [ ] On-demand probe history records
- [ ] Multi-target parallel probing
- [ ] Probe failure intelligent retry mechanism

### Mid-term Plan (3-6 months)
- [ ] Multi-device cluster management
- [ ] OTA firmware update support
- [ ] Edge computing integration
- [ ] Probe task scheduling optimization
- [ ] On-demand probe history records

### On-Demand Probe Feature (Implemented)
- [x] Dynamic on-demand probe endpoint `/probe`
- [x] Support arbitrary hostname/IP addresses
- [x] Optional port parameter support
- [x] Dynamic module parameter selection
- [x] Real-time Prometheus format return