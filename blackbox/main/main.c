/*
 * main.c - ESP32 Blackbox 主程序入口
 *
 * 本文件是 ESP32 Blackbox 应用程序的主入口点，负责初始化系统并启动所有核心服务。
 * 主要职责包括：
 * 1. 初始化非易失性存储 (NVS) 用于保存 WiFi 密码
 * 2. 创建默认事件循环
 * 3. 初始化配置管理器 (加载探测目标) 和 WiFi 管理器
 * 4. 处理 WiFi 密码存储逻辑，决定使用 STA 或 AP 模式
 * 5. 启动配置门户或正常服务
 *
 * 系统流程：
 * - 初始化 NVS
 * - 检查是否存储了 WiFi 密码
 *   - 是：尝试 STA 模式连接
 *     - 连接成功：启动探测管理和指标服务器
 *     - 连接失败：切换到 AP 配置门户
 *   - 否：直接启动 AP 配置门户
 * - 在 AP 模式下，用户连接后可配置 WiFi 密码
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "web_server.h"
#include "config_manager.h"
#include "probe_manager.h"
#include "metrics_server.h"
#include "board_test.h"
#include "status_led.h"

static const char *TAG = "MAIN";

static void print_board_info(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32 Blackbox - Board Info");
    ESP_LOGI(TAG, "  Target: " CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "  IDF Version: %s", esp_get_idf_version());
#if CONFIG_IDF_TARGET_ESP32C3
    ESP_LOGI(TAG, "  Board: ESP32-C3 SuperMini (Nologo)");
    ESP_LOGI(TAG, "  Arch: RISC-V single-core 160MHz");
    ESP_LOGI(TAG, "  SRAM: 400KB, Flash: 4MB (embedded)");
#elif CONFIG_IDF_TARGET_ESP32C6
    ESP_LOGI(TAG, "  Board: Seeed Studio XIAO ESP32C6");
    ESP_LOGI(TAG, "  Arch: RISC-V dual-core (HP 160MHz + LP 20MHz)");
    ESP_LOGI(TAG, "  SRAM: 512KB, Flash: 4MB (external)");
    ESP_LOGI(TAG, "  WiFi: 802.11ax (WiFi 6) + BLE 5.3 + Zigbee/Thread");
#else
    ESP_LOGI(TAG, "  Board: Unknown / Unsupported");
#endif
    ESP_LOGI(TAG, "========================================");
}

void app_main(void)
{
    /* 初始化 NVS (非易失性存储) 用于保存 WiFi 密码 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 创建默认事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "ESP32 Blackbox Starting...");
    print_board_info();

    /* 调试：打印 NVS 中的 WiFi 凭证内容 */
    config_manager_nvs_dump();

    /* 初始化配置管理器和 WiFi 管理器 */
    config_manager_init();
    /* 初始化 LED 状态指示器 */
    status_led_init();
    wifi_manager_init();

    /* 检查是否已存储 WiFi 密码 */
    if (config_manager_wifi_has_credentials()) {
        /* 存储了密码 - 尝试 STA 模式连接 */
        ESP_LOGI(TAG, "WiFi credentials found in NVS, connecting...");

        ret = wifi_manager_start_sta();
        if (ret == ESP_OK) {
#ifdef CONFIG_ESP_BOARD_TEST
            {
                board_test_result_t test_result = {0};
                ESP_LOGI(TAG, "Running board self-test...");
                status_led_set_state(STATUS_LED_SELF_TEST);
                board_test_run(&test_result);
                if (test_result.total_fail > 0) {
                    ESP_LOGW(TAG, "Board self-test had %d failures, continuing startup...",
                             test_result.total_fail);
                }
            }
#endif

            /* 连接成功 - 启动探测管理和指标服务器 */
            probe_manager_init();
            probe_manager_start();
            metrics_server_start();
            web_server_start();
            ESP_LOGI(TAG, "All services started");
            return;
        }

        /* STA 连接失败 - 切换到 AP 配置门户 */
        ESP_LOGW(TAG, "STA failed, falling back to AP config portal");
        wifi_manager_stop();
    } else {
        ESP_LOGI(TAG, "No WiFi credentials - starting AP config portal");
    }

    /* 启动 AP 模式配置门户 */
    ret = wifi_manager_start_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: AP mode failed to start (err=0x%x), restarting in 5s...", ret);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ret = wifi_config_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: Config server failed to start (err=0x%x)", ret);
    }

    ESP_LOGI(TAG, "Config portal active - connect to AP '%s' and open 192.168.4.1",
             CONFIG_ESP_AP_SSID);
}
