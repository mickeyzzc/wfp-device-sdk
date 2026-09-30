/*
 * board_test.c - 硬件自检模块
 *
 * 在目标板卡上运行一系列硬件功能测试，验证关键子系统是否正常工作。
 * 测试项包括：NVS 存储、WiFi 初始化、WiFi 扫描、DNS 解析、HTTP 探测、TCP 探测、指标服务器。
 * 主要用于 ESP32-C6 (Seeed XIAO) 板卡验证，也兼容 ESP32-C3。
 */

#include "board_test.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_http_client.h"

static const char *TAG = "BOARD_TEST";

#define TEST_NVS_NAMESPACE "brd_test"
#define TEST_NVS_KEY       "test_key"

static bool test_nvs(void)
{
    ESP_LOGI(TAG, "[1/7] 测试 NVS 存储...");

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[1/7] NVS 打开失败: %s", esp_err_to_name(err));
        return false;
    }

    uint32_t write_val = 0xDEADBEEF;
    err = nvs_set_u32(nvs, TEST_NVS_KEY, write_val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[1/7] NVS 写入失败: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return false;
    }

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[1/7] NVS 提交失败: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return false;
    }

    uint32_t read_val = 0;
    err = nvs_get_u32(nvs, TEST_NVS_KEY, &read_val);

    if (err != ESP_OK || read_val != write_val) {
        ESP_LOGE(TAG, "[1/7] NVS 读回失败: err=%s, expected=0x%lX, got=0x%lX",
                 esp_err_to_name(err), (unsigned long)write_val, (unsigned long)read_val);
        nvs_erase_key(nvs, TEST_NVS_KEY);
        nvs_commit(nvs);
        nvs_close(nvs);
        return false;
    }

    nvs_erase_key(nvs, TEST_NVS_KEY);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "[1/7] NVS 存储: PASS");
    return true;
}

static bool test_wifi_init(void)
{
    ESP_LOGI(TAG, "[2/7] 测试 WiFi 初始化...");

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "[2/7] netif 初始化失败: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "[2/7] WiFi 初始化失败: %s", esp_err_to_name(ret));
        return false;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "[2/7] WiFi MAC: " MACSTR, MAC2STR(mac));

    ESP_LOGI(TAG, "[2/7] WiFi 初始化: PASS");
    return true;
}

static bool test_wifi_scan(void)
{
    ESP_LOGI(TAG, "[3/7] 测试 WiFi 扫描...");

    /* 如果 WiFi 已经在运行（STA 已连接），跳过扫描避免断开连接 */
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_STA) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "[3/7] WiFi 已连接 SSID: %s, 跳过扫描, PASS", ap.ssid);
            return true;
        }
    }

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[3/7] WiFi 设置 STA 模式失败: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[3/7] WiFi 启动失败: %s", esp_err_to_name(ret));
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    wifi_scan_config_t scan_cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = false,
    };

    ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[3/7] WiFi 扫描失败: %s", esp_err_to_name(ret));
        esp_wifi_stop();
        esp_wifi_deinit();
        return false;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    esp_wifi_stop();
    esp_wifi_deinit();

    if (ap_count == 0) {
        ESP_LOGW(TAG, "[3/7] WiFi 扫描: 未发现 AP (可能无信号)");
        return false;
    }

    ESP_LOGI(TAG, "[3/7] WiFi 扫描: 发现 %d 个 AP, PASS", ap_count);
    return true;
}

static bool test_dns_resolve(void)
{
    ESP_LOGI(TAG, "[4/7] 测试 DNS 解析...");

    struct hostent *host = gethostbyname("dns.google");
    if (host == NULL || host->h_addr_list[0] == NULL) {
        return false;
    }

    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    ESP_LOGI(TAG, "[4/7] DNS 解析: dns.google -> %s, PASS", inet_ntoa(addr));
    return true;
}

static bool test_http_probe(void)
{
    ESP_LOGI(TAG, "[5/7] 测试 HTTP 探测...");

    esp_http_client_config_t config = {
        .url = "http://httpbin.org",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "[5/7] HTTP 客户端初始化失败");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[5/7] HTTP 请求失败: %s", esp_err_to_name(err));
        return false;
    }

    if (status < 200 || status >= 400) {
        ESP_LOGE(TAG, "[5/7] HTTP 状态码异常: %d", status);
        return false;
    }

    ESP_LOGI(TAG, "[5/7] HTTP 探测: status=%d, PASS", status);
    return true;
}

static bool test_tcp_probe(void)
{
    ESP_LOGI(TAG, "[6/7] 测试 TCP 探测...");

    /* 直接连接 Google DNS 8.8.8.8:53，无需 DNS 解析 */
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "[6/7] TCP: 套接字创建失败");
        return false;
    }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    inet_aton("8.8.8.8", &dest_addr.sin_addr);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    close(sock);

    if (err != 0) {
        ESP_LOGE(TAG, "[6/7] TCP 连接 8.8.8.8:53 失败");
        return false;
    }

    ESP_LOGI(TAG, "[6/7] TCP 探测: 8.8.8.8:53 连接成功, PASS");
    return true;
}

static bool test_metrics_server(void)
{
    ESP_LOGI(TAG, "[7/7] 测试指标服务器端口绑定...");

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "[7/7] 套接字创建失败");
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9090);
    addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int err = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);

    if (err != 0) {
        ESP_LOGE(TAG, "[7/7] 端口 9090 绑定失败");
        return false;
    }

    ESP_LOGI(TAG, "[7/7] 指标服务器端口绑定: PASS");
    return true;
}

esp_err_t board_test_run(board_test_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(board_test_result_t));

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32 Blackbox - 硬件自检");
    ESP_LOGI(TAG, "  芯片: %s, 核数: %d, 修订: v%d.%d",
             CONFIG_IDF_TARGET, chip_info.cores,
             chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "========================================");

    result->nvs_ok = test_nvs();
    vTaskDelay(pdMS_TO_TICKS(100));

    result->wifi_init_ok = test_wifi_init();
    vTaskDelay(pdMS_TO_TICKS(100));

    result->wifi_scan_ok = test_wifi_scan();
    vTaskDelay(pdMS_TO_TICKS(100));

    result->dns_resolve_ok = test_dns_resolve();
    vTaskDelay(pdMS_TO_TICKS(100));

    result->http_probe_ok = test_http_probe();
    vTaskDelay(pdMS_TO_TICKS(100));

    result->tcp_probe_ok = test_tcp_probe();
    vTaskDelay(pdMS_TO_TICKS(100));

    result->metrics_server_ok = test_metrics_server();

    result->total_pass = result->nvs_ok + result->wifi_init_ok +
                         result->wifi_scan_ok + result->dns_resolve_ok +
                         result->http_probe_ok + result->tcp_probe_ok +
                         result->metrics_server_ok;
    result->total_fail = 7 - result->total_pass;

    board_test_print_report(result);

    return (result->total_fail == 0) ? ESP_OK : ESP_FAIL;
}

void board_test_print_report(const board_test_result_t *result)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  自检报告: %d/7 通过, %d/7 失败",
             result->total_pass, result->total_fail);
    ESP_LOGI(TAG, "  [1] NVS 存储:     %s", result->nvs_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [2] WiFi 初始化:  %s", result->wifi_init_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [3] WiFi 扫描:    %s", result->wifi_scan_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [4] DNS 解析:     %s", result->dns_resolve_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [5] HTTP 探测:    %s", result->http_probe_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [6] TCP 探测:     %s", result->tcp_probe_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  [7] 指标服务器:   %s", result->metrics_server_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "========================================");

    if (result->total_fail == 0) {
        ESP_LOGI(TAG, "  所有测试通过! 板卡功能正常。");
    } else {
        ESP_LOGW(TAG, "  存在 %d 项失败，请检查硬件连接和网络配置。",
                 result->total_fail);
    }
}
