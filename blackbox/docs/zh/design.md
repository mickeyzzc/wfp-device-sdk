[English](../en/design.md) | **中文**

# ESP32 Blackbox 设计文档

## 设计目标

1. **黑盒导出器兼容**: 完全兼容 blackbox_exporter 的标签体系和抓取模式
2. **模块化架构**: 探测协议与配置分离，支持灵活的参数组合
3. **配置热加载**: 无需重启即可更新探测配置
4. **零配置启动**: 首次启动 AP 模式，Web 页面配置 WiFi
5. **资源优化**: 针对 ESP32-C3/C6 400-512KB SRAM 优化设计

## 技术选型

### ESP-IDF v6.0

选择 ESP-IDF v6.0 的关键原因:
- mbedTLS v4 + PSA Crypto API，更现代的加密框架
- 移除了旧的 `entropy.h`, `ctr_drbg.h`，简化 TLS 配置
- 更完善的 WiFi AP/STA 切换支持
- 原生支持 NVS 非易失性存储
- 更好的 RISC-V (ESP32-C3/C6) 支持

### HTTP 框架选择

**原始方案**: LwIP raw API

**最终方案**: esp_http_server (LWIP + 标准 HTTP)

**选择理由**:
1. **API 简单**: esp_http_server 提供标准 HTTP 服务器接口
2. **功能完整**: 自动处理 HTTP 协议细节
3. **稳定可靠**: 经过 ESP-IDF 充分测试
4. **资源友好**: 相比 raw API 更节省内存

### 配置存储选择

**候选方案**:
- NVS 键值对存储
- 文件系统存储
- SPIFFS + JSON 配置文件

**选择 SPIFFS + JSON**:
1. **配置复杂度**: JSON 支持复杂数据结构（模块、目标数组）
2. **可读性**: 人类可读的配置文件
3. **工具链**: 丰富的 JSON 处理库（cJSON）
4. **扩展性**: 易于添加新的配置字段
4. **调试友好**: 可直接查看文件内容

### 目标硬件支持

| 项目 | ESP32-C3 SuperMini | Seeed XIAO ESP32C6 |
|------|--------------------|--------------------|
| 芯片 | ESP32-C3 (QFN32) | ESP32-C6FH4 (QFN32) |
| 架构 | RISC-V 单核 160MHz | RISC-V 双核 (HP 160MHz + LP 20MHz) |
| Flash | 4MB (内嵌) | 4MB (内嵌) |
| SRAM | 400KB | 512KB |
| WiFi | 802.11 b/g/n | 802.11ax (WiFi 6) |
| BLE | Bluetooth 5 LE | Bluetooth 5.3 LE |
| 其他 | - | Zigbee, Thread (IEEE 802.15.4) |
| 分区表 | 默认 (1MB app) | 自定义 (1.875MB app) |

### 协议栈选择

| 协议 | 实现文件 | 核心依赖 | 特性 |
|------|----------|----------|------|
| HTTP/HTTPS | `probe_http.c` | esp_http_client | 支持自定义方法、状态码验证 |
| TCP | `probe_tcp.c` | lwip/sockets | 基础 TCP 连接测试 |
| TCP+TLS | `probe_tcp.c` | lwip/sockets + mbedtls v4 | TLS 握手计时 |
| DNS | `probe_dns.c` | lwip/netdb | DNS 解析响应时间 |
| ICMP Ping | `probe_icmp.c` | lwip/sockets + lwip/icmp | 原始套接字，RTT 测量 |
| WebSocket | `probe_ws.c` | lwip/sockets | 基础 WebSocket 连接 |
| WebSocket Secure | `probe_ws.c` | lwip/sockets + mbedtls v4 | TLS + WebSocket 握手计时 |

## 核心设计决策

### 1. 模块化探测架构

**设计问题**: 如何支持多种探测协议且配置灵活？

**解决方案**: 模块系统设计

**设计要点**:
- **分离配置**: 协议参数配置与具体探测目标分离
- **引用机制**: 目标通过 `module_name` 引用模块配置
- **类型分发**: probe_manager 根据模块类型分发到对应实现

**实现示例**:
```c
// 模块定义 - 告诉系统如何探测 HTTP
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

// 目标定义 - 使用模块
static probe_target_t s_targets[] = {
    {
        .name = "google_homepage",
        .target = "google.com",
        .port = 80,
        .interval_ms = 30000,
        .module_name = "http_2xx"  // 引用上面的模块配置
    }
};
```

### 2. SPIFFS 配置热加载

**设计问题**: 如何实现运行时配置更新？

**解决方案**: 版本计数器 + SPIFFS 重载

**核心机制**:
- **配置版本**: `static uint8_t s_config_version`
- **原子更新**: 配置验证通过后 s_config_version++
- **事件驱动**: 每个探测循环检查版本变化
- **同步生效**: 版本变化自动触发配置重新加载

**工作流程**:
```c
// 探测任务主循环
void probe_manager_loop(void) {
    // 检查配置版本变化
    if (config_get_version() != s_config_version) {
        ESP_LOGI(TAG, "Config version changed, reloading...");
        probe_manager_reload_targets();
        s_config_version = config_get_version();
    }
    // 执行探测
    for (int i = 0; i < target_count; i++) {
        probe_target_t *target = &targets[i];
        const probe_module_t *module = config_get_module_by_name(target->module_name);
        execute_probe(module, target);
    }
    // 等待间隔
    vTaskDelay(pdMS_TO_TICKS(interval_ms));
}
```

### 4. 统一 Web 服务器架构

**设计问题**: 如何同时支持 AP 模式配置和即席探测？

**解决方案**: 单一服务器，支持配置管理和即席探测

**双模式设计**:
- **AP 模式**: WiFi 凭据配置 (`/`, `/scan`, `/save`)
- **即席探测**: `/probe?target=X&module=Y&port=P` 端点
- **STA 模式**: 配置管理仪表板 (`/api/status`, `/api/config`, `/reload`)

**路由逻辑**:
```c
// Web 服务器路由分发
if (wifi_manager_is_connected()) {
    // STA 模式 - 配置管理和即席探测
    web_server_handle_probe_request();     // /probe 端点
    web_server_start_config_api();        // /api/* 端点
} else {
    // AP 模式 - WiFi 配置
    wifi_config_server_start();           // /scan, /save 端点
}
```

### 5. 原始 ICMP 实现

**设计问题**: ESP-IDF v6.0 如何实现 ICMP ping？

**解决方案**: lwIP 原始套接字

**技术要点**:
- **SOCK_RAW + IPPROTO_ICMP**: 使用 lwIP 原始套接字发送 ICMP
- **校验和计算**: 手动实现 ICMP 校验和算法
- **包匹配**: 使用任务句柄作为 ICMP ID 确保包唯一性

**实现优势**:
- **lwIP 兼容**: 与现有网络栈集成良好
- **低延迟**: 直接网络访问，无需额外协议层
- **可靠**: 实现 Echo Request/Reply 完整流程

## 性能优化设计

### 内存管理

| 组件 | 内存占用 | 优化策略 |
|------|----------|----------|
| 探测任务 | 16KB 栈 | 避免动态分配，使用栈变量 |
| 配置系统 | 静态数组 | 固定最大限制，避免 OOM |
| TLS 栈 | 8KB | 使用 PSA Crypto，减少熵源开销 |
| JSON 处理 | 复用缓冲区 | 限制配置文件大小 (64KB) |
| 网络缓冲 | lwIP 默认 | 合理设置 SO_RCVTIMEO |

### 探测调度优化

**串行执行策略**:

- **优势**: 实现简单，资源使用可预测
- **优势**: 避免多线程竞争条件
- **劣势**: 无法并行探测不同目标
- **平衡**: 通过合理间隔满足大部分使用场景

**间隔设置**:
```c
// 最小间隔保护 (防止 watchdog 超时)
#define MIN_PROBE_INTERVAL_MS    5000

// 自动间隔调整 (如果目标未指定 interval)
if (target->interval_ms == 0) {
    target->interval_ms = s_config.scrape_interval_ms;
}
```

### 错误处理策略

**静默失败模式**:
- 单个探测失败不影响其他探测任务
- 错误信息记录在 result.error_msg 中
- 通过 metrics 暴露失败状态
- 系统整体保持稳定运行

**错误分类处理**:
```c
// 超时错误
if (result.success == false && strstr(result.error_msg, "timeout")) {
    // 可能是网络问题，下次重试
}

// 连接错误
if (result.success == false && strstr(result.error_msg, "connection")) {
    // 可能是服务不可用，标记为失败
}

// 协议错误
if (result.success == false && strstr(result.error_msg, "protocol")) {
    // 配置问题，需要检查配置
}
```

## 安全性设计

### WiFi 安全
- **AP 模式**: WPA2-PSK 加密，默认密码 `12345678`（局域网使用）
- **STA 模式**: 用户配置 WPA2/WPA3 加密网络
- **NVS 存储**: WiFi 凭据加密存储，安全传输

### 配置文件安全
- **文件权限**: SPIFFS 配置文件仅系统可读写
- **输入验证**: 严格验证 JSON 格式和数据范围
- **防止溢出**: 固定大小缓冲区，使用 strncpy/snprintf

### 网络服务安全
- **AP 模式**: 仅在有客户端连接时运行，完成后自动关闭
- **STA 模式**: 配置管理仅限局域网访问
- **输入净化**: 所有用户输入都经过长度和格式检查

### TLS 安全性

**mbedTLS v4 + PSA Crypto**:
- 自动密钥管理，无需手动处理随机数
- 支持 SNI (Server Name Indication)
- 可选证书验证（通过 verify_ssl 配置）
- 分阶段计时，便于性能分析

## 扩展性设计

### 添加新探测协议

**步骤 1**: 更新配置头文件
```c
// config_manager.h
typedef enum {
    MODULE_HTTP,      // 已有
    MODULE_TCP,       // 已有
    MODULE_DNS,       // 已有
    MODULE_ICMP,      // 已有
    MODULE_NEW_PROTO, // 新增
} probe_module_type_t;
```

**步骤 2**: 实现探测函数
```c
// probe_new_proto.c
probe_result_t probe_new_proto_execute(const probe_target_t *target,
                                      const probe_module_config_t *module_config) {
    // 实现新协议探测逻辑
}
```

**步骤 3**: 更新模块配置
```c
// config_manager.c
typedef struct {
    uint32_t timeout_ms;
    // 新协议特定配置
    new_proto_config_t new_proto;
} module_config_union_t;
```

**步骤 4**: 注册到配置系统
```c
// config_manager.c - 工厂默认配置
static const probe_module_t s_factory_modules[] = {
    {
        .name = "new_proto_module",
        .config = {
            .type = MODULE_NEW_PROTO,
            .timeout_ms = 10000,
            .config.new_proto = {/* 默认配置 */}
        }
    }
};
```

### 配置系统扩展

**添加新配置字段**:
```json
 "modules": {
   "existing_module": {
      "prober": "http",
      "timeout": 10,
      "http": {
        "method": "GET",
        "new_field": "value"  // 新增字段
      }
    }
  },
  "targets": [...]
}
```

**向后兼容性**:
- 新字段使用默认值，旧配置仍可正常运行
- 移除的字段在升级时自动使用默认值
- 版本控制确保配置平滑迁移

## 监控指标设计

### 指标分类

| 指标名称 | 类型 | 标签 | 用途 |
|----------|------|------|------|
| probe_duration_seconds | Gauge | target, module | 探测总耗时 |
| probe_success | Gauge | target, module | 探测是否成功 |
| probe_status_code | Gauge | target, module | HTTP 状态码或结果码 |
| probe_tls_handshake_seconds | Gauge | target, module | TLS 握手耗时 (仅适用) |
| probe_icmp_rtt_ms | Gauge | target, module | ICMP 往返时间 (仅 ICMP) |
| probe_icmp_packets_sent | Gauge | target, module | ICMP 发送包数 (仅 ICMP) |
| probe_icmp_packets_received | Gauge | target, module | ICMP 接收包数 (仅 ICMP) |

### 标签设计原则

1. **目标标识**: `target="目标名称"` 提供业务逻辑标识
2. **模块标识**: `module="模块名称"` 提供技术类型标识  
3. **避免污染**: 不使用通用标签如 `host`, `instance`
4. **字段稳定**: 标签名称和含义保持向后兼容

### 指标示例

**HTTP 探测指标**:
```
 HELP probe_duration_seconds Duration of the probe in seconds
 TYPE probe_duration_seconds gauge
probe_duration_seconds{target="google_http", module="http_2xx"} 0.234

 HELP probe_success Whether the probe succeeded
 TYPE probe_success gauge
probe_success{target="google_http", module="http_2xx"} 1

 HELP probe_status_code Status code or result code
 TYPE probe_status_code gauge
probe_status_code{target="google_http", module="http_2xx"} 200
```

**ICMP 探测指标**:
```
 HELP probe_duration_seconds Duration of the probe in seconds
 TYPE probe_duration_seconds gauge
probe_duration_seconds{target="ping_host", module="icmp_ping"} 0.025

 HELP probe_success Whether the probe succeeded
 TYPE probe_success gauge
probe_success{target="ping_host", module="icmp_ping"} 1

 HELP probe_icmp_rtt_ms Round-trip time for ICMP ping in milliseconds
 TYPE probe_icmp_rtt_ms gauge
probe_icmp_rtt_ms{target="ping_host", module="icmp_ping"} 15.2

 HELP probe_icmp_packets_sent Number of ICMP packets sent
 TYPE probe_icmp_packets_sent gauge
probe_icmp_packets_sent{target="ping_host", module="icmp_ping"} 3

 HELP probe_icmp_packets_received Number of ICMP packets received
 TYPE probe_icmp_packets_received gauge
probe_icmp_packets_received{target="ping_host", module="icmp_ping"} 3
```

## 未来规划

### 已实现特性
- [x] 模块化探测架构 (modules[] + targets[])
- [x] SPIFFS JSON 配置系统
- [x] 配置热加载机制
- [x] ICMP ping 探测实现
- [x] 统一 Web 服务器架构
- [x] 黑盒导出器兼容标签体系
- [x] /probe 端点实时执行

### 短期规划 (1-2 个月)
- [ ] 探测结果历史存储和趋势分析
- [ ] 探测失败智能重试机制
- [ ] 探测健康评分算法
- [ ] WebSocket 实时状态推送
- [ ] 配置文件版本控制和回滚
- [ ] 探测任务调度优化
- [ ] 即席探测历史记录
- [ ] 多目标并行探测
- [ ] 探测失败智能重试机制

### 中期规划 (3-6 个月)
- [ ] 多设备集群管理
- [ ] OTA 固件更新支持
- [ ] 边缘计算集成
- [ ] 探测任务调度优化
- [ ] 即席探测历史记录

### 即席探测功能 (已实现)
- [x] 动态即席探测端点 `/probe`
- [x] 支持任意主机名/IP 地址
- [x] 可选端口参数支持
- [x] 模块参数动态选择
- [x] 实时 Prometheus 格式返回