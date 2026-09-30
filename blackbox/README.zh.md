# ESP32 Blackbox

[English](README.md) | [中文](README.zh.md)

轻量级 ESP32 网络探测设备，兼容 Prometheus blackbox_exporter。通过 JSON 配置或 Web UI 定义探测目标，输出标准 Prometheus 格式指标。

## 支持硬件

| 开发板 | 芯片 | WiFi | 板载 LED |
|--------|------|------|----------|
| ESP32-C3 SuperMini | ESP32-C3 160MHz | 802.11 b/g/n | GPIO8（低电平点亮） |
| Seeed Studio XIAO ESP32C6 | ESP32-C6 160MHz | 802.11ax (WiFi 6) | GPIO15（高电平点亮） |

## 快速上手

### 构建与烧录

```bash
cd blackbox   # 工程位于 wfp-device-sdk 仓库的此子目录

# Python 脚本（推荐，跨平台）
python build.py esp32c6 build
python build.py esp32c6 flash COM3

# 或使用 ESP-IDF 命令行（需先 source export.sh）
idf.py set-target esp32c6
idf.py build
idf.py -p COM3 flash monitor
```

### 首次启动

1. 设备创建 WiFi 热点 `ESP32_Blackbox`（密码：`12345678`）
2. 连接热点，浏览器打开 `http://192.168.4.1`
3. 扫描网络，输入 WiFi 密码，保存
4. 设备自动重启并连接你的 WiFi

## 探测端点

连接成功后，设备提供两个 HTTP 端口：

| 端口 | 端点 | 说明 |
|------|------|------|
| 9090 | `/metrics` | 所有已配置目标的 Prometheus 指标 |
| 9090 | `/probe?target=X&module=Y` | 即席探测（无需修改配置） |
| 80 | `/` | Web 配置管理界面 |

### 即席探测

无需配置，直接探测任意目标：

```bash
# HTTP 探测
curl "http://192.168.61.150:9090/probe?target=httpbin.org&module=http_2xx"

# DNS 解析
curl "http://192.168.61.150:9090/probe?target=8.8.8.8&module=dns"

# TCP 端口测试
curl "http://192.168.61.150:9090/probe?target=example.com&module=tcp&port=443"

# ICMP Ping
curl "http://192.168.61.150:9090/probe?target=8.8.8.8&module=icmp_ping"
```

返回标准 Prometheus 格式：

```
probe_success{target="httpbin.org",module="http_2xx"} 1
probe_duration_seconds{target="httpbin.org",module="http_2xx"} 0.234
```

### Prometheus 集成

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

## 支持的探测模块

| 模块 | 协议 | 说明 |
|------|------|------|
| `http_2xx` | HTTP/HTTPS | HTTP GET/POST，支持状态码验证 |
| `tcp` | TCP | TCP 连接测试 |
| `dns` | DNS | DNS 解析测试 |
| `icmp_ping` | ICMP | ICMP Ping（原生 socket 实现） |
| `ws` | WebSocket | WebSocket 连接测试 |
| `wss` | WebSocket Secure | WebSocket + TLS |

## LED 状态指示

| 状态 | 模式 |
|------|------|
| 启动中 | 快闪 |
| AP 配置模式 | 慢闪 |
| 连接 WiFi | 中速闪 |
| 已连接 | 常亮 |
| 已断开 | 快闪 |
| 连接失败 | 闪 3 次 + 暂停 1 秒 |

## 文档

| 文档 | 说明 |
|------|------|
| [使用指南](docs/zh/usage.md) | 环境搭建、构建烧录、配置方法、Prometheus 集成 |
| [架构设计](docs/zh/architecture.md) | 系统架构、模块设计、数据流 |
| [设计文档](docs/zh/design.md) | 技术选型、探测模块内部实现 |

## 许可证

MIT
