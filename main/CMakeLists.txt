# ESP32 Blackbox 组件 CMake 构建配置文件
# 注册所有源文件、头文件目录和依赖组件

# 注册 ESP-IDF 组件
idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"

        "probe_http.c"
        "probe_tcp.c"
        "probe_dns.c"
        "probe_ws.c"
        "probe_icmp.c"
        "metrics_server.c"
        "config_manager.c"
        "probe_manager.c"
        "board_test.c"
        "status_led.c"
        "web_server.c"
    # 头文件搜索目录
    INCLUDE_DIRS "."
    # 依赖的 ESP-IDF 组件列表
    REQUIRES
        nvs_flash              # 非易失性存储
        esp_wifi               # WiFi 驱动
        esp_http_client        # HTTP 客户端
        esp_http_server        # HTTP 服务器
        esp_netif              # 网络接口（DHCP、IP 地址管理）
        lwip                   # TCP/IP 协议栈
        esp_timer              # 定时器
        json                   # JSON 解析（本地 components/json）
        esp_event              # 事件循环
        log                    # 日志系统
        freertos              # 实时操作系统
        mbedtls               # TLS/SSL 加密库
        spiffs                # SPIFFS 文件系统
        esp_driver_gpio        # GPIO 驱动（LED 控制）
        app_update             # OTA 分区写入（web 刷机）
)
