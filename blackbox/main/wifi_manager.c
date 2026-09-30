#include "wifi_manager.h"
#include "config_manager.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

#define AP_SSID            CONFIG_ESP_AP_SSID
#define AP_PASSWORD        CONFIG_ESP_AP_PASSWORD
#define AP_CHANNEL         6
#define AP_MAX_CONN        4
#define AP_BEACON_INTERVAL 100
#define MAX_STA_RETRY      CONFIG_ESP_MAXIMUM_RETRY
#define STA_TIMEOUT_MS     30000

/**
 * @brief 初始化 WiFi 驱动并创建 STA + AP 网络接口
 *
 * 该函数初始化 ESP-IDF WiFi 模块，创建默认的 STA 和 AP 网络接口，并设置事件处理回调。
 * 必须在调用任何 start_* 函数之前调用。
 */
static EventGroupHandle_t s_wifi_event_group;
static const char *TAG = "WIFI";
static int s_retry_num = 0;
static bool s_sta_connecting = false;

/* ------------------------------------------------------------------ */
/**
 * @brief WiFi 事件处理回调函数
 *
 * 处理所有 WiFi 事件，包括 STA 连接、断开、获取 IP 以及 AP 相关事件。
 * 根据事件类型执行相应的操作，如重新连接、设置标志位等。
 *
 * @param arg 传入的参数（通常为 NULL）
 * @param event_base 事件基类
 * @param event_id 事件 ID
 * @param event_data 事件数据
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "event: base=%s id=%ld", event_base, event_id);

    /* STA 事件处理 - 仅在 STA 连接阶段响应 */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_sta_connecting) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_sta_connecting) {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "STA disconnected - reason=%d, rssi=%d", d->reason, d->rssi);
            if (s_retry_num < MAX_STA_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect (%d/%d)", s_retry_num, MAX_STA_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGW(TAG, "STA connect failed after %d retries", MAX_STA_RETRY);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_sta_connecting) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
    /* AP 事件处理 */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "[AP-EVT] AP started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "[AP-EVT] station connected - MAC: " MACSTR " aid=%d",
                 MAC2STR(e->mac), e->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "[AP-EVT] station disconnected - MAC: " MACSTR, MAC2STR(e->mac));
    }
}
/* ------------------------------------------------------------------ */
/**
 * @brief 打印 AP 状态信息
 *
 * 输出 AP 配置信息、传输功率、连接的客户端数量以及 BSSID 等调试信息。
 */
static void dump_ap_status(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    ESP_LOGI(TAG, "[DEBUG] WiFi mode: %d (AP=2, STA=1, APSTA=3)", mode);

    wifi_config_t cfg_read = {0};
    esp_err_t e = esp_wifi_get_config(WIFI_IF_AP, &cfg_read);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "[DEBUG] AP SSID: '%s' (len=%d)",
                 cfg_read.ap.ssid, cfg_read.ap.ssid_len);
        ESP_LOGI(TAG, "[DEBUG] AP password: '%s' (len=%d)",
                 cfg_read.ap.password, strlen((char *)cfg_read.ap.password));
        ESP_LOGI(TAG, "[DEBUG] AP channel: %d", cfg_read.ap.channel);
        ESP_LOGI(TAG, "[DEBUG] AP authmode: %d (0=OPEN, 3=WPA2_PSK, 4=WPA_WPA2, 5=WPA3)", cfg_read.ap.authmode);
        ESP_LOGI(TAG, "[DEBUG] AP max_conn: %d", cfg_read.ap.max_connection);
        ESP_LOGI(TAG, "[DEBUG] AP ssid_hidden: %d", cfg_read.ap.ssid_hidden);
    } else {
        ESP_LOGE(TAG, "[DEBUG] Failed to read AP config: %s", esp_err_to_name(e));
    }

    int8_t tx_power = 0;
    esp_wifi_get_max_tx_power(&tx_power);
    ESP_LOGI(TAG, "[DEBUG] TX power: %d / 4 = %.1f dBm", tx_power, tx_power / 4.0f);

    wifi_sta_list_t sta_list = {0};
    esp_wifi_ap_get_sta_list(&sta_list);
    ESP_LOGI(TAG, "[DEBUG] Connected stations: %d", sta_list.num);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    ESP_LOGI(TAG, "[DEBUG] AP BSSID: " MACSTR, MAC2STR(mac));

    wifi_country_t country_read = {0};
    esp_wifi_get_country(&country_read);
    ESP_LOGI(TAG, "[DEBUG] Country: cc=%.3s schan=%d nchan=%d max_tx=%d policy=%d",
             country_read.cc, country_read.schan, country_read.nchan,
             country_read.max_tx_power, country_read.policy);
}
/* ------------------------------------------------------------------ */
/**
 * @brief 初始化 WiFi 模块
 *
 * 创建并返回一个事件组句柄，用于跟踪 WiFi 连接状态。
 * 初始化网络接口、WiFi 模块，并注册事件处理回调。
 */
esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init() failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init() failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 设置 WiFi 国家码为中国 (CN)，信道 1-13 可用 */
    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_AUTO
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    ESP_LOGI(TAG, "WiFi country code set to CN, channels 1-13");

    esp_event_handler_instance_t h_any, h_gotip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                    ESP_EVENT_ANY_ID, &event_handler, NULL, &h_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                    IP_EVENT_STA_GOT_IP, &event_handler, NULL, &h_gotip));

    return ESP_OK;
}
/* ------------------------------------------------------------------ */
/**
 * @brief AP 调试任务
 *
 * 定期打印 AP 活跃状态和连接客户端数量，用于调试。
 */
static void ap_debug_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 等待 10 秒
        wifi_sta_list_t stas = {0};
        esp_wifi_ap_get_sta_list(&stas);  // 获取连接的客户端列表
        ESP_LOGI(TAG, "[HEARTBEAT] AP alive, connected stations: %d", stas.num);
    }
}
/* ------------------------------------------------------------------ */
/**
 * @brief 启动 AP 模式
 *
 * 配置并启动 Access Point 模式，创建一个配置门户。
 * 设置 SSID、密码、信道等参数，并启动调试任务。
 */
esp_err_t wifi_manager_start_ap(void)
{
    ESP_LOGI(TAG, "[DEBUG] Compiling AP config...");
    ESP_LOGI(TAG, "[DEBUG] AP_SSID raw: '%s' (strlen=%d)", AP_SSID, strlen(AP_SSID));
    ESP_LOGI(TAG, "[DEBUG] AP_PASSWORD raw: '%s' (strlen=%d)", AP_PASSWORD, strlen(AP_PASSWORD));

    wifi_config_t wifi_config = {0};
    wifi_config.ap.channel = AP_CHANNEL;
    wifi_config.ap.max_connection = AP_MAX_CONN;
    wifi_config.ap.beacon_interval = AP_BEACON_INTERVAL;

    strncpy((char *)wifi_config.ap.ssid, AP_SSID, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, AP_PASSWORD, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = strlen(AP_SSID);
    wifi_config.ap.authmode = (strlen(AP_PASSWORD) >= 8) ? WIFI_AUTH_WPA2_PSK
                                                         : WIFI_AUTH_OPEN;

    ESP_LOGI(TAG, "[DEBUG] wifi_config.ap.ssid = '%s' ssid_len=%u authmode=%d",
              wifi_config.ap.ssid, wifi_config.ap.ssid_len, wifi_config.ap.authmode);

    esp_err_t ret;

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    ESP_LOGI(TAG, "[DEBUG] set_mode(WIFI_MODE_AP) => %s", esp_err_to_name(ret));
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    ESP_LOGI(TAG, "[DEBUG] set_config(AP) => %s", esp_err_to_name(ret));
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_start();
    ESP_LOGI(TAG, "[DEBUG] wifi_start() => %s", esp_err_to_name(ret));
    if (ret != ESP_OK) return ret;

    /* 等待 AP 事件触发后打印状态 */
    vTaskDelay(pdMS_TO_TICKS(1000));
    dump_ap_status();

    /* 每 10 秒打印 AP 状态 */
    wifi_mode_t cur_mode;
    esp_wifi_get_mode(&cur_mode);
    if (cur_mode == WIFI_MODE_AP || cur_mode == WIFI_MODE_APSTA) {
        xTaskCreate(ap_debug_task, "ap_debug", 2048, NULL, 1, NULL);
    }

    ESP_LOGI(TAG, "AP mode started - connect to '%s' (password: '%s')", AP_SSID, AP_PASSWORD);
    return ESP_OK;
}
/* ------------------------------------------------------------------ */
/**
 * @brief 启动 STA 模式
 *
 * 从 NVS 加载 WiFi 凭证，配置并启动 STA 模式，等待连接完成。
 * 支持重试机制，最多重试 MAX_STA_RETRY 次。
 */
esp_err_t wifi_manager_start_sta(void)
{
    s_retry_num = 0;
    s_sta_connecting = true;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    char ssid[64] = {0};
    char password[64] = {0};

    esp_err_t err = config_manager_wifi_load(ssid, sizeof(ssid),
                                             password, sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to load WiFi credentials from NVS");
        return err;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "STA mode - connecting to '%s' ...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdTRUE, pdFALSE, pdMS_TO_TICKS(STA_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to '%s'", ssid);
        s_sta_connecting = false;
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "failed to connect to '%s' after %d retries", ssid, MAX_STA_RETRY);
    } else {
        ESP_LOGW(TAG, "STA connection timed out after %d ms", STA_TIMEOUT_MS);
    }
    return ESP_FAIL;
}
/* ------------------------------------------------------------------ */
/**
 * @brief 停止 WiFi
 *
 * 停止 WiFi 模块，释放相关资源。
 */
esp_err_t wifi_manager_stop(void)
{
    s_sta_connecting = false;

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_stop() failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_LOGI(TAG, "WiFi stopped and state reset");
    return ESP_OK;
}
/* ------------------------------------------------------------------ */
/**
 * @brief 检查是否已连接
 *
 * 检查 STA 是否已连接并获取了 IP 地址。
 */
bool wifi_manager_is_connected(void)
{
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}
