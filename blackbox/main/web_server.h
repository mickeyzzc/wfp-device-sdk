/*
 * web_server.h - 统一 Web 服务器接口
 *
 * 提供 AP 模式 WiFi 配置页面和 STA 模式配置管理仪表板。
 * AP 模式: WiFi 扫描、密码输入、凭证保存到 NVS
 * STA 模式: 设备信息、当前配置展示、JSON 编辑器、探测状态摘要
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

/* 启动 AP 模式配置服务器 (WiFi 配置页面) */
esp_err_t wifi_config_server_start(void);

/* 启动 STA 模式 Web UI (配置管理仪表板) */
esp_err_t web_server_start(void);

/* 停止 STA 模式 Web UI */
esp_err_t web_server_stop(void);

#endif
