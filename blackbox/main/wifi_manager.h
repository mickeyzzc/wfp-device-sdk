/*
 * wifi_manager.h - WiFi 管理器接口
 *
 * 提供 WiFi 模块的高级接口，管理 STA（站点）和 AP（接入点）模式。
 * 使用 FreeRTOS 事件组通知连接状态，封装 ESP-IDF WiFi API。
 *
 * 主要功能：
 * - STA 模式：连接配置的 WiFi 网络，支持自动重连
 * - AP 模式：创建配置门户热点，提供 Web 配置界面
 * - 基于事件的状态更新
 * - 可配置的重试逻辑
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief 初始化 WiFi 驱动并创建 STA + AP 网络接口
 *
 * 必须在调用任何 start_* 函数之前调用一次。
 *
 * @return ESP_OK 初始化成功
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief 启动 AP 模式（WiFi 配置门户）
 *
 * SSID/密码来自 Kconfig (CONFIG_ESP_AP_SSID / CONFIG_ESP_AP_PASSWORD)。
 * 启动后用户可连接热点并打开 192.168.4.1 进行配置。
 *
 * @return ESP_OK 启动成功
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * @brief 启动 STA 模式（连接路由器）
 *
 * 使用 NVS 中存储的 WiFi 凭证进行连接。
 * 阻塞等待直到连接成功 (ESP_OK) 或所有重试耗尽 (ESP_FAIL)。
 *
 * @return ESP_OK 连接成功，ESP_FAIL 连接失败
 */
esp_err_t wifi_manager_start_sta(void);

/**
 * @brief 停止 WiFi 模块
 *
 * 切换模式前必须先调用此函数停止当前 WiFi。
 *
 * @return ESP_OK 停止成功
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief 检查 STA 是否已连接并获取 IP
 *
 * @return true 已连接，false 未连接
 */
bool wifi_manager_is_connected(void);

#endif
