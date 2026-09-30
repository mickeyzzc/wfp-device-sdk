[English](../en/architecture.md) | **中文**

# ESP32 Blackbox 架构设计

## 系统概述

ESP32 Blackbox 是一个基于 ESP32-C3/C6 的嵌入式网络探测系统，采用模块化设计，支持首次启动 AP 配置模式。系统以黑盒导出器兼容架构为核心，实现了灵活的探测模块系统和 SPIFFS 配置管理。

```
┌─────────────────────────────────────────────────────┐
│                Application Layer                    │
│  ┌─────────────┐  ┌───────────────────────────┐    │
│  │    main.c    │  │   Unified Web Server     │    │
│  └─────────────┘  │  (STA Dashboard + AP UI) │    │
├─────────────────────────────────────────────────────┤
│                Service Layer                        │
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
           │  动态即席探测端点            │          │
           │  /probe?target=X&          │          │
           │     module=Y&            │          │
           │        port=P              │          │
           └────────────────────────────┘          │
├─────────────────────────────────────────────────────┤
                Config Layer                         │
  ┌─────────────────────────────────────────────┐    │
│       Config Manager (SPIFFS + JSON)        │    │
│  • 模块系统 (modules[])                      │    │
│  • 目标定义 (targets[])                      │    │
│  • 热加载支持                              │    │
└─────────────────────────────────────────────┘    │
```

## 核心架构特性

### 1. 黑盒导出器兼容设计

系统完全兼容 blackbox_exporter 的探测模式，遵循 Prometheus 生态系统标准：

- **标签体系**: `target="目标名称"`, `module="模块名称"` (而非 `target/port/type`)
- **指标格式**: 标准 Prometheus 文本格式，易于集成
- **/probe 端点**: 支持实时执行、即席探测和 blackbox_exporter 风格的抓取配置
- **即席探测**: 支持任意主机名/IP 地址，可选端口参数

### 2. 模块化探测系统

协议探测与配置分离的设计：

- **模块定义**: 每个协议的配置参数在 `modules[]` 中定义
- **目标引用**: 目标通过 `module_name` 引用模块，实现配置复用
- **类型分发**: probe_manager 根据目标引用的模块类型执行对应探测

### 3. SPIFFS 配置层

JSON 配置文件的灵活管理：

- **持久化存储**: SPIFFS 文件系统保存完整配置
- **热加载**: 支持 POST /api/config 和 POST /reload 动态更新
- **工厂默认**: 配置文件缺失时自动使用内置默认配置

## 核心模块详解

### 1. WiFi Manager (`wifi_manager.c/h`)

负责 WiFi 连接管理，支持 AP 和 STA 双模式。

**功能:**
- 初始化 WiFi，自动检测 NVS 中是否已存储 WiFi 凭据
- AP 模式: 启动热点供用户配置 WiFi (SSID: `ESP32_Blackbox`, 密码: `12345678`)
- STA 模式: 连接已配置的 WiFi 网络
- 事件驱动连接管理，自动重连机制
- 连接状态查询

**关键设计:**
- 使用 FreeRTOS EventGroup 同步连接状态
- 首次启动无凭据时自动进入 AP 模式
- NVS 存储 WiFi SSID 和密码，重启后自动连接
- 支持配置最大重试次数 (默认 5 次)

### 2. Config Manager (`config_manager.c/h`)

统一管理系统配置，实现模块化探测架构。

**核心数据结构:**
```c
// 模块定义: 协议配置的集合
typedef struct {
    char name[32];                              // 模块名称
    probe_module_config_t config;               // 模块配置
} probe_module_t;

// 目标定义: 引用模块的具体探测任务
typedef struct {
    char name[64];                              // 目标名称  
    char target[256];                          // 目标地址
    uint16_t port;                             // 目标端口
    uint32_t interval_ms;                      // 探测间隔
    char module_name[32];                       // 引用的模块名称
} probe_target_t;

// 主配置结构
typedef struct {
    probe_module_t *modules;                    // 模块数组
    uint8_t module_count;                       // 模块数量
    probe_target_t *targets;                    // 目标数组
    uint8_t target_count;                       // 目标数量
    uint32_t scrape_interval_ms;               // 全局抓取间隔
    uint16_t metrics_port;                     // Metrics 端口
} blackbox_config_t;
```

**功能:**
- SPIFFS 文件系统挂载和 JSON 配置读写
- 模块和目标的配置验证
- 热加载支持 (s_config_version 计数器)
- 工厂默认配置生成
- NVS WiFi 凭证存储 (保持独立)

**配置文件格式:**
```json

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
```

### 3. Probe Manager (`probe_manager.c/h`)

探测任务调度器，实现模块化协议探测。

**职责:**
- 管理探测任务的生命周期
- 根据目标引用的模块类型执行对应协议
- 轮询执行所有配置的探测
- 存储探测结果供 metrics 服务器使用

**任务流程:**
```
while (1) {
    for (每个探测目标) {
        const probe_module_t *module = config_get_module_by_name(target.module_name)
        执行 module->config.type 对应的探测函数
        保存结果
    }
    等待 interval_ms
}
```

### 4. 探测模块

各协议的探测实现，每个模块独立维护配置参数。

| 模块 | 类型 | 文件 | 支持功能 |
|------|------|------|----------|
| HTTP | `MODULE_HTTP` | `probe_http.c` | HTTP GET/POST, 状态码验证 |
| HTTPS | `MODULE_HTTPS` | `probe_http.c` | TLS 加密, 证书验证 |
| TCP | `MODULE_TCP` | `probe_tcp.c` | TCP 连接测试 |
| TCP+TLS | `MODULE_TCP_TLS` | `probe_tcp.c` | TLS 握手计时 |
| DNS | `MODULE_DNS` | `probe_dns.c` | DNS 解析测试 |
| ICMP Ping | `MODULE_ICMP` | `probe_icmp.c` | Ping 测试, RTT 测量 |
| WebSocket | `MODULE_WS` | `probe_ws.c` | WebSocket 连接测试 |
| WebSocket Secure | `MODULE_WSS` | `probe_ws.c` | TLS + WebSocket 握手计时 |

**探测结果结构:**
```c
typedef struct {
    bool success;               // 是否成功
    uint32_t duration_ms;       // 总耗时
    int status_code;            // 状态码/结果代码
    char error_msg[128];        // 错误信息
    union {
        struct { /* HTTP 详情: connect_time, tls_time, ttfb, status */ } http;
        struct { /* TCP 详情: connect_time, tls_time */ } tcp;
        struct { /* DNS 详情: resolve_time, resolved_ip */ } dns;
        struct { /* ICMP 详情: packets_sent, packets_received, rtt_ms */ } icmp;
        struct { /* WebSocket 详情: connect_time, tls_time, handshake_time */ } ws;
    } details;
} probe_result_t;
```

### 5. 统一 Web 服务器 (支持 AP + STA + 即席探测)

统一的 Web 服务器，提供三种模式的服务：

**AP 模式 (WiFi 配置门户):**
- 端口 80，暗色主题配置页面
- WiFi 扫描和凭据保存
- `/`, `/scan`, `/save` 路由

**即席探测模式 (动态执行):**
- `/probe?target=X&module=Y&port=P` 实时探测
- 支持任意主机名和 IP 地址
- 返回 Prometheus 格式的即时结果

**STA 模式 (配置管理仪表板):**
- 设备状态和配置编辑界面
- `/api/status`, `/api/config`, `/api/reload` API
- 实时探测状态显示

**路由分发逻辑:**
```c
if (wifi_manager_is_connected()) {
    // STA 模式 - 配置管理和即席探测
    web_server_handle_probe_request();    // /probe 端点
    web_server_start_config_api();       // /api/* 端点
} else {
    // AP 模式 - WiFi 配置
    wifi_config_server_start();           // /scan, /save 端点
}
```

  │
  ├─ print_board_info()            // 打印板卡信息 (C3/C6)
  │
  ├─ board_test_run()              // 硬件自检 (C6 默认开启, C3 默认关闭)
  │
  ├─ config_manager_init()         // SPIFFS 挂载 → 配置加载
  │   ├─ mount_spiffs()            // 挂载 SPIFFS 文件系统
  │   ├─ config_load_from_spiffs() // 读取 /spiffs/blackbox.json
  │   ├─ config_validate()        // 验证配置有效性
  │   └─ 配置失败时使用工厂默认
  │
  ├─ wifi_manager_init()
  │   ├─ 检查 NVS 中是否有 WiFi 凭据
  │   │   ├─ 无凭据 → 启动 AP 模式 → wifi_config_server_start()
  │   │   │                    // Web 配置门户 (端口 80)
  │   │   └─ 等待用户配置
  │   │
  │   └─ 有凭据 → 启动 STA 模式 → 等待 WiFi 连接
  │       └─ 连接成功 → web_server_start()
  │                       // STA 模式仪表板 (端口 80)
  │
  ├─ probe_manager_init()
  │   └─ probe_manager_start()     // 启动探测任务
  │
  └─ metrics_server_start()        // 启动 HTTP 服务器 (端口 9090)
```

## 线程模型

系统使用 FreeRTOS 多任务架构，每个任务有明确职责：

| 任务名 | 优先级 | 栈大小 | 功能 |
|--------|--------|--------|------|
| probe_task | 5 | 16KB | 执行网络探测 (含 TLS) |
| metrics_server | 5 | 8KB | Prometheus HTTP 服务器 (端口 9090) |
| web_server | 5 | 8KB | STA 模式配置仪表板 (端口 80) |
| wifi_config_server | 5 | 4KB | AP 模式配置 Web 服务器 (端口 80) |
| WiFi events | 系统 | - | WiFi 事件处理 |

## 网络服务架构

### HTTP 服务端点

#### /metrics 端点 (Prometheus 抓取)
```
GET /metrics
HTTP/1.1 200 OK
Content-Type: text/plain; version=0.0.4

 HELP probe_duration_seconds Duration of the probe in seconds
 TYPE probe_duration_seconds gauge
probe_duration_seconds{target="google_http", module="http_2xx"} 0.234
probe_duration_seconds{target="google_dns", module="dns_resolve"} 0.089

 HELP probe_success Whether the probe succeeded
 TYPE probe_success gauge
probe_success{target="google_http", module="http_2xx"} 1
probe_success{target="google_dns", module="dns_resolve"} 1

 HELP probe_tls_handshake_seconds Duration of TLS handshake in seconds
 TYPE probe_tls_handshake_seconds gauge
probe_tls_handshake_seconds{target="google_https", module="https"} 0.128

 HELP probe_icmp_rtt_ms Round-trip time for ICMP ping in milliseconds
 TYPE probe_icmp_rtt_ms gauge
probe_icmp_rtt_ms{target="ping_host", module="icmp_ping"} 15.2
```

#### /probe 端点 (实时执行)
```
GET /probe?target=httpbin.org&module=http_2xx
HTTP/1.1 200 OK
Content-Type: text/plain; version=0.0.4

 HELP probe_duration_seconds Duration of the probe in seconds
 TYPE probe_duration_seconds gauge
probe_duration_seconds{target="httpbin.org", module="http_2xx"} 0.234

 HELP probe_success Whether the probe succeeded
 TYPE probe_success gauge
probe_success{target="httpbin.org", module="http_2xx"} 1

 HELP probe_status_code Status code or result code
 TYPE probe_status_code gauge
probe_status_code{target="httpbin.org", module="http_2xx"} 200
```

### 配置管理 API

#### STA 模式 Web 服务器 (端口 80)
```
GET /api/status          // 设备状态 JSON
POST /api/config        // 更新配置 JSON
GET /api/config         // 获取当前配置 JSON
POST /api/reload         // 热加载配置
GET /                   // 配置管理仪表板页面
```

#### AP 模式配置门户 (端口 80)
```
GET /                   // WiFi 配置页面
GET /scan               // WiFi 扫描结果 JSON
POST /save              // 保存 WiFi 凭证
```

## 数据流架构

```
SPIFFS 配置文件] ──→ Config Manager ──→ 模块系统
                                       │
    ┌─────────────────┐               │
    │ 工厂默认配置      │               │
    └─────────────────┘               │
                                       │
─→ Probe Manager ←─ Target References  │
        │                              │
        └─→ 各协议探测模块             │
                    │                  │
                    └─→ 结果存储         │
                                      │
─→ Metrics Server ←─┐                │
                   │                │
─→ Prometheus 抓取  ┼─→ /probe 端点  │
                   │                │
─→ Web UI 配置    ┼─→ /api/config   │
```

## 配置热加载机制

### 版本计数器
```
// 配置版本管理
static uint8_t s_config_version = 0;

// 热加载函数
esp_err_t config_reload(void) {
    // 从 SPIFFS 重新加载配置
    // 验证配置有效性
    s_config_version++;
    return ESP_OK;
}
```

### 配置更新流程
```
POST /api/config (Web UI)

─ 读取 JSON 请求体
─ 调用 config_update_targets()
   ├─ 解析 JSON 到内存
─ 验证配置有效性
─ 保存到 SPIFFS
─ 更新 s_config_version++

POST /reload (API 或直接调用)
─ 调用 config_reload()
   ├─ 重新读取 SPIFFS
─ 验证和生效
```

## 性能与资源考虑

### 内存布局

| 组件 | 估计内存 |
|------|----------|
| WiFi 栈 | ~40KB |
| LwIP | ~20KB |
| Probe Task | ~16KB 栈 (含 TLS) |
| Metrics Server | ~8KB 栈 |
| Web Server Task | ~8KB 栈 |
| Config Manager | ~4KB 栈 |
| TLS (mbedTLS v4 / PSA) | ~25KB |
| App 分区 | ~960KB (94% 使用) |

### 探测性能

- **最小探测间隔**: 5000ms (防止 watchdog 超时)
- **HTTP/HTTPS**: 10-30秒超时
- **TCP**: 5-10秒超时
- **TCP+TLS/WSS**: 10-15秒超时 (TLS 握手耗时)
- **DNS**: 5秒超时
- **ICMP**: 5秒超时

### 并发模型

采用串行探测模型，避免资源竞争：

- probe_task 单线程轮询执行所有目标
- 无并发探测，确保稳定性
- 间隔执行，防止内存和 CPU 过载

## 扩展计划

### 已完成
- [x] SPIFFS JSON 配置系统
- [x] 模块化探测架构
- [x] ICMP ping 探测
- [x] 热加载配置机制
- [x] 统一 Web 服务器 (AP + STA + 即席探测)
- [x] /probe 端点实时执行
- [x] 黑盒导出器兼容标签体系
- [x] 即席探测功能 - 支持任意主机/IP 和端口参数

### 即席探测架构
- 支持动态目标配置和实时执行
- 模块系统支持协议参数动态选择
- Prometheus 兼容格式返回结果
- 无需修改配置文件即可进行网络测试

### 长期规划
- [ ] 多设备集群管理
- [ ] OTA 固件更新
- [ ] 边缘计算集成
- [ ] 云端配置同步