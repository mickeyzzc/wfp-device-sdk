[English](../en/usage.md) | **中文**

# ESP32 Blackbox 使用指南

## 目录

- [环境准备](#环境准备)
- [支持的开发板](#支持的开发板)
- [构建与烧录](#构建与烧录)
- [首次配置 (AP 模式)](#首次配置-ap-模式)
- [配置文件格式](#配置文件格式)
- [Web UI 配置](#web-ui-配置)
- [验证运行](#验证运行)
- [硬件自检](#硬件自检)
- [Prometheus 集成](#prometheus-集成)
- [故障排除](#故障排除)

## 环境准备

### 1. 安装 ESP-IDF v6.0

```bash
# 克隆 ESP-IDF v6.0
git clone --recursive -b v6.0 https://github.com/espressif/esp-idf.git

# 安装依赖
./install.sh

# 激活环境
. ./export.sh
```

要求 ESP-IDF **v6.0** (mbedTLS v4 + PSA Crypto API)。

### 2. 验证环境

```bash
idf.py --version
```

## 支持的开发板

| 项目 | ESP32-C3 SuperMini | Seeed XIAO ESP32C6 |
|------|--------------------|--------------------|
| 芯片 | ESP32-C3 (QFN32) | ESP32-C6FH4 (QFN32) |
| 架构 | RISC-V 单核 160MHz | RISC-V 双核 (HP 160MHz + LP 20MHz) |
| SRAM | 400KB | 512KB |
| Flash | 4MB (内嵌) | 4MB (内嵌) |
| WiFi | 802.11 b/g/n | 802.11ax (WiFi 6) |
| BLE | Bluetooth 5 LE | Bluetooth 5.3 LE |
| 其他 | - | Zigbee, Thread (IEEE 802.15.4) |
| USB | USB-Serial/JTAG | USB-Serial/JTAG |
| 自检 | 默认关闭 | 默认开启 |
| 分区表 | 默认 (1MB app) | 自定义 (1.875MB app) |

## 构建与烧录

### 方式一：Python 构建脚本（推荐）

无需手动运行 ESP-IDF export.bat，脚本自动配置工具链路径：

```bash
# ESP32-C6 构建并烧录
python build.py esp32c6 flash COM6

# ESP32-C3 构建并烧录
python build.py esp32c3 flash COM3

# 仅构建（不烧录）
python build.py esp32c6 build

# 全量清理后重新构建
python build.py esp32c6 clean
```

**完整参数**：
```
python build.py <target> [action] [port]

target:  esp32c3 | esp32c6
action:  build | flash | monitor | clean  (默认: build)
port:    COM 端口号，如 COM3、COM6       (默认: 自动检测)
```

### 方式二：ESP-IDF 命令行

需先运行 ESP-IDF 的 export.bat 激活环境：

```bash
# 首次构建 - 设置目标芯片（会自动 fullclean + 加载 sdkconfig.defaults.<target>）
idf.py set-target esp32c6        # 或 esp32c3

# 编译
idf.py build

# 烧录
idf.py -p COM6 flash

# 烧录 + 串口监控（Ctrl+] 退出）
idf.py -p COM6 flash monitor

# 清理
idf.py fullclean
```

### 方式三：预编译固件（GitHub Releases）

每次发布版本会自动构建固件并发布到 GitHub Releases。

1. 从 [GitHub Releases](https://github.com/micke/esp32-blackbox/releases) 下载固件
2. 使用 esptool 烧录:

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

### 切换目标芯片

从 C3 切换到 C6（或反之）时，**必须重新设置目标**：

```bash
idf.py set-target esp32c6    # 自动执行 fullclean + 重新生成 sdkconfig
```

ESP-IDF 会自动加载对应的 `sdkconfig.defaults.<target>` 文件。

### sdkconfig 分层机制

ESP-IDF 构建系统按以下顺序加载配置：

```
sdkconfig.defaults              ← 通用配置（WiFi AP、FreeRTOS、LWIP 等）
sdkconfig.defaults.<target>     ← 目标特定配置（Flash、加密、分区表）
```

| 文件 | 内容 |
|------|------|
| `sdkconfig.defaults` | WiFi AP SSID/密码、重试次数、FreeRTOS、LWIP、日志级别 |
| `sdkconfig.defaults.esp32c3` | C3 Flash 4MB QIO、硬件加密加速 |
| `sdkconfig.defaults.esp32c6` | C6 Flash 4MB QIO、硬件加密加速、自定义分区表 |

### COM 端口识别

Windows 下查看可用串口：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

一般规则：
- XIAO ESP32C6 通常出现在 `COM5` ~ `COM8`
- ESP32-C3 SuperMini 通常出现在 `COM3` ~ `COM6`
- 同一时间只接一块板子可避免端口混淆

## 首次配置 (AP 模式)

设备首次启动时，NVS 中没有 WiFi 凭据，会自动进入 AP 配置模式。

### 步骤

1. **给设备上电**，等待约 5 秒启动完成
2. **打开手机/电脑 WiFi 设置**，搜索并连接以下热点:
   - SSID: `ESP32_Blackbox`
   - 密码: `12345678`
3. **打开浏览器**，访问 `http://192.168.4.1`
4. **点击「扫描 WiFi」** 按钮，等待附近 WiFi 列表加载
5. **选择你的 WiFi**，输入密码
6. **点击「保存并重启」**
7. 设备会自动保存凭据到 NVS 并重启
8. 重启后设备进入 STA 模式，自动连接你配置的 WiFi

### 配置页面功能

- WiFi 扫描: 列出附近的 WiFi 网络及信号强度
- 手动输入: 也可手动输入 SSID (适用于隐藏 WiFi)
- 信号指示: 显示各网络的信号强度
- 保存重启: 凭据写入 NVS，设备重启进入 STA 模式

### 重新配置

如需重新配置 WiFi，有两种方式:

**方式 1: 擦除 NVS (推荐)**
```bash
idf.py -p COM6 erase-flash
idf.py -p COM6 flash
```

**方式 2: 长按复位键**
- 按住 RST/EN 按钮不放，设备重启后可进入重新配置模式

## 配置文件格式

ESP32 Blackbox 使用 SPIFFS 文件系统存储 JSON 配置文件。配置文件路径为 `/spiffs/blackbox.json`。

### 配置文件结构

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

### 字段说明

#### 全局配置字段

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|---------|------|
| scrape_interval | number | 否 | 30 | 抓取间隔（秒） |
| metrics_port | number | 否 | 9090 | Metrics 服务器端口 |

#### 模块配置 (modules)

模块配置定义了各种探测协议的具体参数。

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| prober | string | 是 | 模块类型：http, https, tcp, tcp_tls, dns, icmp, ws, wss |
| timeout | number | 否 | 超时时间（秒） |
| http | object | 否 | HTTP 模块特定配置 |
| tcp | object | 否 | TCP 模块特定配置 |
| dns | object | 否 | DNS 模块特定配置 |
| icmp | object | 否 | ICMP 模块特定配置 |

**HTTP 模块配置**:

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

字段说明:
- method: HTTP 方法 (GET, HEAD, POST, PUT, DELETE)
- valid_status_codes: 有效的 HTTP 状态码数组
- no_follow_redirects: 是否不跟随重定向

**TCP 模块配置**:

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

字段说明:
- tls: 是否使用 TLS (仅 tcp_tls 类型有效)
- query: 发送的查询字符串
- expected_response: 期望的响应字符串

**DNS 模块配置**:

```json
  "prober": "dns",
  "timeout": 5,
  "dns": {
    "query_name": "httpbin.org",
    "query_type": 1
  }
}
```

字段说明:
- query_name: 要查询的域名
- query_type: 查询类型 (1=A, 28=AAAA, 2=NS 等)

**ICMP 模块配置**:

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

字段说明:
- packets: 发送的 ICMP 数据包数量
- payload_size: 数据负载大小 (字节)
- pattern: 负载填充模式 (0-255)

#### 目标配置 (targets)

目标配置引用模块定义来执行具体的探测任务。

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| name | string | 是 | 目标名称（用于指标标签） |
| target | string | 是 | 目标主机名或 IP 地址 |
| port | number | 是 | 目标端口 (ICMP 可为 0) |
| module | string | 是 | 引用的模块名称 |
| interval | number | 否 | 探测间隔（秒），不使用则用全局 scrape_interval |

#### 模块名称映射

prober 字串对应的模块类型：

| prober | 模块类型 | 描述 |
|--------|----------|------|
| "http" | MODULE_HTTP | HTTP 明文连接 |
| "https" | MODULE_HTTPS | HTTPS 加密连接 |
| "tcp" | MODULE_TCP | TCP 连接测试 |
| "tcp_tls" | MODULE_TCP_TLS | TLS 加密连接测试 |
| "dns" | MODULE_DNS | DNS 解析测试 |
| "icmp" | MODULE_ICMP | ICMP ping 测试 |
| "ws" | MODULE_WS | WebSocket 连接测试 |
| "wss" | MODULE_WSS | WebSocket Secure 连接测试 |

## Web UI 配置

设备成功连接 WiFi 后，会启动 STA 模式的 Web 配置管理界面。

### 访问配置界面

1. 确认设备已连接 WiFi，获取设备 IP 地址
2. 在浏览器中访问 `http://<设备IP>:80`

### 界面功能

#### 设备信息面板

- **IP 地址**: 设备当前的网络 IP 地址
- **WiFi SSID**: 当前连接的 WiFi 网络名称
- **运行时间**: 设备已运行的时间
- **模块数量**: 当前配置的探测模块数量
- **目标数量**: 当前配置的探测目标数量
- **抓取间隔**: 全局抓取间隔设置
- **Metrics 端口**: 指标服务器端口

#### 探测状态表格

| 列 | 说明 |
|------|------|
| 名称 | 目标配置的名称 |
| 目标 | 探测的目标地址 |
| 端口 | 目标端口 |
| 状态 | 探测结果状态 (OK/FAIL) |
| 耗时 | 探测耗时 |
| 错误 | 错误信息 (如有) |

#### 配置编辑器

- **JSON 编辑器**: 显示当前配置的完整 JSON
- **保存**: 保存配置到 SPIFFS 文件
- **重新加载**: 从 SPIFFS 重新加载配置（热加载）
- **热重载**: 重新加载配置但不验证格式化

### API 接口

#### 获取设备状态

```bash
curl http://<设备IP>:80/api/status
```

响应示例:
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

#### 获取配置文件

```bash
curl http://<设备IP>:80/api/config
```

返回完整的 JSON 配置内容，可用于备份。

#### 更新配置

```bash
curl -X POST http://<设备IP>:80/api/config \
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

#### 热加载配置

```bash
curl -X POST http://<设备IP>:80/api/reload
```

#### 响应说明

- **状态码**: 200 表示成功，4xx/5xx 表示失败
- **错误信息**: 失败时会返回具体的错误原因
- **热加载**: 成功后配置会立即生效，无需重启

## /probe 端点使用

### 实时执行探测

系统提供了 `/probe` 端点用于实时执行探测，返回 Prometheus 格式的指标。

#### 使用方法

```bash
#curl "http://<设备IP>:9090/probe?target=<主机名或IP>&module=<模块名称>"
#curl "http://<设备IP>:9090/probe?target=<主机名或IP>&module=<模块名称>&port=<端口号>"
```

#### 参数说明

| 参数 | 必需 | 说明 |
|------|------|------|
| target | 是 | 目标主机名或 IP 地址（支持任意主机，无需预配置） |
| module | 是 | 模块名称 |
| port | 否 | 目标端口号，可选。如果为 0 或省略，模块使用默认端口 |

#### 新增功能说明

**重要更新**: `/probe` 端点现在支持**任意主机名和 IP 地址**的 ad-hoc 探测，不再局限于预配置的目标名称。这是与 Prometheus blackbox_exporter 兼容的重要改进。

**端口参数**: 新增可选的 `port` 参数，支持指定目标端口。如果不指定，各模块使用默认端口：
- HTTP/HTTPS: 80/443
- TCP: 根据模块配置
- DNS: 53  
- ICMP: 0（不使用端口）

#### 使用示例

**HTTP 探测 (ad-hoc 目标)**:
```bash
# 探测任意 HTTP 服务
curl "http://192.168.1.100:9090/probe?target=www.baidu.com&module=http_2xx&port=80"

# 探测 HTTPS 服务
curl "http://192.168.1.100:9090/probe?target=github.com&module=https_2xx&port=443"
```

**ICMP 探测 (ad-hoc 目标)**:
```bash
# 探测网关连通性
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=icmp_ping"

# 探测外部主机
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=icmp_ping"
```

**TCP 探测 (ad-hoc 目标)**:
```bash
# 探测端口连通性
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=tcp_connect&port=80"
curl "http://192.168.1.100:9090/probe?target=smtp.gmail.com&module=tcp_connect&port=587"
```

**DNS 探测 (ad-hoc 目标)**:
```bash
# 测试 DNS 服务器
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=dns_resolve&port=53"
```

### Prometheus 抓取配置

使用 `/probe` 端点配置 Prometheus 抓取任务：

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
          - example.com:80    # 黑盒导出器风格的 relabel 配置
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090  # 设备 IP

  - job_name: 'blackbox_icmp'
    metrics_path: /probe
    params:
      module: [icmp_ping]
    static_configs:
      - targets:
          - 192.168.1.1:0    # ICMP 探测使用端口 0
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: 192.168.1.100:9090  # 设备 IP

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
        replacement: 192.168.1.100:9090  # 设备 IP
```

## 验证运行

### AP 模式 (首次启动)

正常启动日志:
```
I (xxx) MAIN: ESP32 Blackbox Starting...
I (xxx) MAIN:   Target: esp32c6
I (xxx) MAIN:   Board: Seeed Studio XIAO ESP32C6
I (xxx) WIFI: AP mode started - connect to 'ESP32_Blackbox'
```

### STA 模式 (已配置 WiFi)

正常启动日志:
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

### 获取设备 IP

在监视器日志中查看:
```
I (xxx) WIFI: got ip:192.168.1.100
```

### 访问服务端点

```bash
 Web 配置界面
curl http://192.168.1.100:80/

 设备状态 API
curl http://192.168.1.100:80/api/status

 Prometheus 指标
curl http://192.168.1.100:9090/metrics

 实时探测执行
curl "http://192.168.1.100:9090/probe?target=www.baidu.com&module=http_2xx&port=80"
```

## 硬件自检

ESP32-C6 默认在启动时运行 7 项硬件自检，验证板卡功能：

| # | 测试项 | 验证内容 |
|---|--------|----------|
| 1 | NVS 存储 | 写入→读回→验证→清除 |
| 2 | WiFi 初始化 | netif + WiFi 驱动 + MAC 地址 |
| 3 | WiFi 扫描 | STA 模式扫描周围 AP |
| 4 | DNS 解析 | 解析 httpbin.org |
| 5 | HTTP 探测 | 请求 httpbin.org |
| 6 | TCP 探测 | 连接 8.8.8.8:53 |
| 7 | 指标服务器 | 端口 9090 绑定 |

**注意**：自检需要 WiFi 网络连接才能完成 DNS/HTTP/TCP 测试。首次启动（AP 模式）时这些测试会失败，这是正常的。

通过 `idf.py menuconfig` → ESP32 Blackbox Configuration → Run board self-test on startup 调整开关。

## Prometheus 集成

### 1. 安装 Prometheus

参考: https://prometheus.io/download/

### 2. 配置 Prometheus

#### 方式一：使用 /metrics 端点（传统方式）

编辑 `prometheus.yml`:

```yaml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

scrape_configs:
  - job_name: 'esp32-blackbox'
    static_configs:
      - targets: ['192.168.1.100:9090']
```

### 3. 启动 Prometheus

```bash
./prometheus --config.file=prometheus.yml
```

### 4. 访问 Prometheus UI

打开浏览器访问: http://localhost:9090

### 5. 查询指标

在 Expression 框中输入查询:

```
 传统抓取方式
probe_success
probe_duration_seconds
probe_status_code

 /probe 端点方式
probe_success{module="http_2xx"}
probe_duration_seconds{module="icmp_ping"}
probe_icmp_rtt_ms
```

### Grafana 集成 (可选)

1. 安装 Grafana: https://grafana.com/docs/grafana/latest/setup/
2. 添加 Prometheus 数据源
3. 创建 Dashboard

**Grafana 查询示例**:
```promql
# 探测成功率按模块统计
sum(probe_success) by (module)

# 平均响应时间按目标
avg(probe_duration_seconds) by (target)

# ICMP 响应时间分布
histogram_quantile(0.95, probe_icmp_rtt_ms_bucket)

# 探测成功率趋势
rate(probe_success[5m])
```

## 使用教程

本节提供详细的实际使用案例，帮助用户快速上手 ESP32 Blackbox 的各种探测场景。

### 1. TCP 连通性测试

**用途**: 测试网络设备的端口可达性，常用于检查网关、路由器、服务等是否在线。

**示例**: 测试网关 HTTP 端口是否开放

```bash
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=tcp_connect&port=80"
```

**预期结果**:
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

**分析**: `probe_success=1` 表示端口可达，`probe_duration_seconds` 显示连接耗时。

**进阶测试**: 检查 SMTP 服务
```bash
curl "http://192.168.1.100:9090/probe?target=smtp.gmail.com&module=tcp_connect&port=587"
```

### 2. HTTP 服务可用性检测

**用途**: 验证 Web 服务是否正常响应，检测 HTTP 状态码和响应时间。

**示例**: 检测百度首页是否可访问

```bash
curl "http://192.168.1.100:9090/probe?target=www.baidu.com&module=http_2xx&port=80"
```

**预期结果**:
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

**分析**: `probe_status_code=200` 表示 HTTP 200 OK 响应。

**HTTPS 测试**: 检测 GitHub
```bash
curl "http://192.168.1.100:9090/probe?target=github.com&module=https_2xx&port=443"
```

### 3. DNS 解析验证

**用途**: 验证 DNS 服务器是否正常工作，域名解析是否成功。

**示例**: 测试公共 DNS 服务器

```bash
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=dns_resolve&port=53"
```

**预期结果**:
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

**分析**: `probe_success=1` 表示 DNS 解析成功。

**自定义域名测试**:
```bash
curl "http://192.168.1.100:9090/probe?target=www.example.com&module=dns_resolve&port=53"
```

### 4. ICMP Ping 测试

**用途**: 测试网络延迟和连通性，常用于监控网络质量和主机可用性。

**示例**: 测试网关延迟

```bash
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=icmp_ping"
```

**预期结果**:
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

**分析**: `probe_icmp_rtt_ms` 显示往返时间毫秒数，`probe_icmp_packets_received` 显示成功接收包数。

**外部主机测试**:
```bash
curl "http://192.168.1.100:9090/probe?target=8.8.8.8&module=icmp_ping"
```

### 5. Prometheus blackbox_exporter 集成

**用途**: 完整集成 Prometheus 监控系统，实现自动化监控告警。

#### Prometheus 配置文件

创建 `prometheus-blackbox.yml`:

```yaml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

rule_files:
  - "blackbox_rules.yml"

scrape_configs:
# HTTP 服务监控
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
        replacement: 192.168.1.100:9090  # ESP32 设备 IP
      - source_labels: [__param_target]
        regex: (.*):.*
        target_label: port
        replacement: $1

# TCP 端口监控
- job_name: 'blackbox_tcp'
    metrics_path: /probe
    params:
      module: [tcp_connect]
    static_configs:
      - targets:
          - 192.168.1.1:80    # 网关 HTTP
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

# ICMP Ping 监控
- job_name: 'blackbox_icmp'
    metrics_path: /probe
    params:
      module: [icmp_ping]
    static_configs:
      - targets:
          - 192.168.1.1:0    # 网关
          - 8.8.8.8:0        # Google DNS
          - www.baidu.com:0   # 百度
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

# ESP32 设备状态监控
- job_name: 'esp32_blackbox'
    metrics_path: /metrics
    static_configs:
      - targets: ['192.168.1.100:9090']
```

#### 告警规则

创建 `blackbox_rules.yml`:

```yaml
groups:
  - name: blackbox_alerts
    rules:
# # HTTP 服务不可用
- alert: HttpProbeFailed
    expr: probe_success{job="blackbox_http"} == 0
    for: 1m
    labels:
      severity: critical
    annotations:
      summary: "HTTP 服务不可用: {{ $labels.instance }}"
      description: "HTTP 服务 {{ $labels.instance }} 连续 1 分钟不可用"

# # TCP 端口不可用
- alert: TcpPortProbeFailed
    expr: probe_success{job="blackbox_tcp"} == 0
    for: 1m
    labels:
      severity: warning
    annotations:
      summary: "TCP 端口不可用: {{ $labels.instance }}"
      description: "TCP 端口 {{ $labels.instance }} 连续 1 分钟不可用"

# # ICMP Ping 超时
- alert: IcmpProbeFailed
    expr: probe_success{job="blackbox_icmp"} == 0
    for: 2m
    labels:
      severity: warning
    annotations:
      summary: "ICMP Ping 失败: {{ $labels.instance }}"
      description: "主机 {{ $labels.instance }} ICMP Ping 连续 2 分钟失败"

# # 响应时间过长
- alert: HighResponseTime
    expr: probe_duration_seconds{job="blackbox_http"} > 2
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "高响应时间: {{ $labels.instance }}"
      description: "HTTP 服务 {{ $labels.instance }} 响应时间超过 2 秒，持续 5 分钟"

# # 包丢失率过高
- alert: HighPacketLoss
    expr: (probe_icmp_packets_sent - probe_icmp_packets_received) / probe_icmp_packets_sent > 0.2
    for: 2m
    labels:
      severity: warning
    annotations:
      summary: "高丢包率: {{ $labels.instance }}"
      description: "ICMP 探测 {{ $labels.instance }} 丢包率超过 20%，持续 2 分钟"
```

### 6. 定时探测 vs 按需探测

#### 预配置目标（定时探测）

通过配置文件设置定期执行的探测任务：

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

**特点**:
- 系统自动定期执行
- 配置持久化存储
- 支持不同间隔时间
- 适合长期监控

#### 临时探测（按需探测）

通过 `/probe` 端点进行即时探测：

```bash
# 临时检查某个服务状态
curl "http://192.168.1.100:9090/probe?target=www.example.com&module=http_2xx&port=80"

# 故障时快速诊断
curl "http://192.168.1.100:9090/probe?target=192.168.1.1&module=icmp_ping"

# 测试新部署的服务
curl "http://192.168.1.100:9090/probe?target=new-service.local&module=http_2xx&port=8080"
```

**特点**:
- 即时执行，无需等待调度
- 支持任意目标
- 临时使用，不保存配置
- 适合故障诊断和测试

### 7. 使用 curl 命令行测试

#### 基础测试命令

**设备状态检查**:
```bash
curl -s http://192.168.1.100:9090/metrics | grep probe_success | head -5
```

**单个 HTTP 探测**:
```bash
curl -s "http://192.168.1.100:9090/probe?target=httpbin.org&module=http_2xx&port=80"
```

**带详细输出的 HTTP 探测**:
```bash
curl -s "http://192.168.1.100:9090/probe?target=httpbin.org&module=http_2xx&port=80" | \
  grep -E "(probe_success|probe_duration_seconds|probe_status_code)"
```

**批量测试脚本**:
```bash
#!/bin/bash
ESP32_IP="192.168.1.100"

# 测试多个目标
targets=(
    "www.baidu.com:80:http_2xx"
    "httpbin.org:80:http_2xx"
    "8.8.8.8:53:dns_resolve"
    "192.168.1.1:0:icmp_ping"
)

for target_info in "${targets[@]}"; do
    IFS=':' read -r host port module <<< "$target_info"
    echo "测试 $host:$port ($module)..."
    curl -s "${ESP32_IP}:9090/probe?target=${host}&module=${module}&port=${port}"
    echo "---"
done
```

#### 性能测试

**连续探测测试**:
```bash
# 连续 10 次探测同一目标
for i in {1..10}; do
    echo "第 $i 次探测:"
    curl -s "http://192.168.1.100:9090/probe?target=httpbin.org&module=http_2xx&port=80" | \
      grep probe_duration_seconds
    sleep 1
done
```

**并发测试**:
```bash
# 并发执行多个探测（使用 xargs）
echo "www.baidu.com:80:http_2xx" \
     "httpbin.org:80:http_2xx" \
     "8.8.8.8:53:dns_resolve" | \
xargs -n3 -P3 -I {} bash -c '
    host=$(echo {} | cut -d: -f1)
    port=$(echo {} | cut -d: -f2)
    module=$(echo {} | cut -d: -f3)
    echo "测试 $host:$port ($module)..."
    curl -s "192.168.1.100:9090/probe?target=${host}&module=${module}&port=${port}" | \
      grep probe_success
'
```

### 8. 在浏览器中查看探测结果

#### 直接访问 Web 界面

1. **设备状态页面**:
```
http://192.168.1.100:9090/
```
显示设备基本信息和简单的状态摘要。

2. **完整指标页面**:
```
http://192.168.1.100:9090/metrics
```
显示所有 Prometheus 格式的指标数据（纯文本格式）。

3. **实时探测页面**:
```
http://192.168.1.100:9090/probe?target=httpbin.org&module=http_2xx&port=80
```
直接在浏览器中执行单次探测并返回结果。

#### Web UI 配置管理

1. **访问配置管理界面**:
```
http://192.168.1.100:/
```
（STA 模式下）

2. **查看实时状态**:
- 设备 IP 地址和运行时间
- 当前配置的模块和目标数量
- 各目标的最新探测结果
- 响应时间和成功率统计

3. **配置管理**:
- 查看当前 JSON 配置
- 编辑配置文件
- 保存并热加载配置

#### 浏览器调试技巧

**查看原始指标数据**:
```javascript
// 在浏览器控制台中执行
fetch('http://192.168.1.100:9090/metrics')
  .then(response => response.text())
  .then(data => {
    console.log('所有指标数据:');
    console.log(data);
  });
```

**测试特定探测**:
```javascript
// 测试 HTTP 探测
fetch('http://192.168.1.100:9090/probe?target=httpbin.org&module=http_2xx&port=80')
  .then(response => response.text())
  .then(data => {
    console.log('HTTP 探测结果:');
    console.log(data);
  });
```

**实时监控**:
```javascript
// 每 5 秒刷新一次状态
setInterval(() => {
  fetch('http://192.168.1.100:9090/metrics')
    .then(response => response.text())
    .then(data => {
      const successLines = data.match(/probe_success\{.*\} 1/g) || [];
      const totalTargets = successLines.length;
      console.log(`当前活跃目标: ${totalTargets}`);
    });
}, 5000);
```

### 总结

本教程涵盖了 ESP32 Blackbox 的各种使用场景：

- **基础连通性测试**: TCP、ICMP、DNS
- **服务可用性检测**: HTTP/HTTPS 服务状态
- **系统集成**: Prometheus 监控配置和告警规则
- **使用模式**: 预配置定时探测 vs 按需临时探测
- **命令行工具**: curl 批量测试和性能测试
- **浏览器访问**: Web UI 使用和调试技巧

通过这些实际案例，用户可以根据自己的需求选择合适的探测方式，快速诊断网络问题，建立完整的监控体系。

## 故障排除

### 搜索不到 AP 热点

**症状**：手机/电脑搜不到 `ESP32_Blackbox` WiFi 热点。

**原因 1：board_test WiFi 初始化冲突（已修复）**

**修复**：`board_test.c` 在 WiFi 扫描测试后增加 `esp_wifi_deinit()` 完整反初始化；`wifi_manager_init()` 对 `esp_netif_init()` 和 `esp_wifi_init()` 的 `ESP_ERR_INVALID_STATE` 做容错处理。

**原因 2：设备未完成启动**

1. 确认设备已上电并完成启动 (等待 5 秒)
2. 检查串口日志确认 AP 模式已启动
3. 靠近设备，WiFi 信号范围有限

**原因 3：NVS 数据异常**

```bash
idf.py -p COM6 erase-flash
idf.py -p COM6 flash
```

### App 分区溢出

**症状**：构建时报 `app partition is too small for binary`。

**原因**：ESP32-C6 的固件体积超过默认分区表的 1MB app 限制。

**修复**：项目使用自定义分区表 `partitions.csv`，将 app 分区扩大到 1.875MB。该配置在 `sdkconfig.defaults.esp32c6` 中自动生效：

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

### 工具链版本不匹配

**症状**：构建时报 `Tool doesn't match supported version from list ['esp-15.2.0_20251204']`。

**原因**：ESP-IDF v6.0 需要 `esp-15.2.0_20251204` 工具链，旧版 `esp-14.2.0` 不兼容。

**修复**：运行 ESP-IDF 安装器更新工具链：

```bash
cd $IDF_PATH
./install.sh esp32c6    # 安装 C6 所需工具
```

或手动检查 `C:\Espressif\tools\riscv32-esp-elf\` 下是否有 `esp-15.2.0_20251204` 目录。

### WiFi 连接失败

1. 检查 NVS 中的 SSID 和密码是否正确
2. 确认 WiFi 信号强度
3. 检查是否有特殊字符需要转义
4. 擦除 NVS 重新配置: `idf.py erase-flash`

### 配置文件问题

**症状**：配置文件损坏或格式错误

**解决方案**:
1. 通过 Web UI 的"重新加载"按钮重新从 SPIFFS 加载
2. 检查 JSON 格式是否正确
3. 删除 `/spiffs/blackbox.json` 文件让系统使用工厂默认配置

**文件检查**:
```bash
 通过串口监视器查看 SPIFFS 状态
#NTidf.py -p COM6 monitor

 在监视器中查看配置日志
#YNI (xxx) CFG_MGR: SPIFFS: 总空间=XXXX 字节, 已用=YYYY 字节
#XZI (xxx) CFG_MGR: JSON 解析完成: X 模块, Y 目标
#RM```

### 探测全部失败

1. 确认目标服务器可达
2. 检查防火墙设置
3. 增加 timeout_ms 值
4. 检查配置文件中的模块引用是否正确

### Metrics 服务无响应

1. 确认设备已获取 IP
2. 检查 metrics_port 配置
3. 确认端口未被占用

### /probe 端点错误

1. 确认目标名称和模块名称存在
2. 检查参数格式是否正确
3. 查看 Web UI 中的配置是否正确

**常见错误**:
- `target not found`: 目标名称不存在（注意：现在支持任意主机名/IP）
- `module not found`: 模块名称不存在
- `probe timeout`: 探测超时，尝试增加超时时间
- `invalid target`: 目标配置无效

### Web UI 访问问题

1. 确认设备 IP 地址正确
2. 检查端口 80 是否被占用
3. 确认设备已连接 STA 模式

### 编译错误

1. 确保 ESP-IDF v6.0 安装完整
2. 目标已设置: `idf.py set-target esp32c6`
3. 更新子模块: `git submodule update --init --recursive`
4. 清理并重新编译: `idf.py fullclean && idf.py build`
5. 确认 `components/json/` 目录存在 (v6.0 本地 cJSON 组件)

### TLS 握手失败

1. 确认目标端口支持 TLS (通常为 443)
2. 如需证书验证, 确保设备时间同步 (NTP)
3. 尝试设置 `verify_ssl = false` 排除证书问题
4. 检查日志中的 mbedTLS 错误码 (格式 `-0xXXXX`), 常见错误:
   - `-0x2700`: 证书验证失败
   - `-0x7200`: SSL 握手超时
   - `-0x0050`: 网络发送失败

### XIAO ESP32C6 Flash 大小

部分批次的 XIAO ESP32C6 配备 8MB Flash（非 4MB）。如果烧录时 esptool 报 Flash 大小不匹配，修改 `sdkconfig.defaults.esp32c6`：

```
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
```

然后执行 `python build.py esp32c6 clean` 重新构建。

### 配置热加载问题

如果配置更新后没有生效：

1. 检查配置文件的 JSON 格式是否正确
2. 确认模块引用是否存在
3. 查看 Web UI 中的"重新加载"按钮是否执行成功
4. 重启设备让配置完全生效

**调试信息**:
```bash
 通过串口查看配置版本变化
#VYidf.py -p COM6 monitor

#XWI (xxx) CFG_MGR: 热加载配置...
#XWI (xxx) CFG_MGR: JSON 解析完成: X 模块, Y 目标
#XWI (xxx) CFG_MGR: 配置已热加载: version=Z
#YM```