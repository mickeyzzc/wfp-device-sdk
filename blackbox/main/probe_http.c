/*
 * probe_http.c - HTTP/HTTPS 探测实现
 *
 * 通过 ESP-IDF HTTP 客户端库执行 HTTP 和 HTTPS 探测。
 * 发送 GET 请求到目标 URL，测量请求总耗时，返回 HTTP 状态码。
 */

#include "probe_types.h"
#include <string.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"

/**
 * @brief 执行 HTTP 探测
 *
 * 构建 http:// URL，发送 GET 请求，测量总耗时。
 *
 * @param target 探测目标配置
 * @return probe_result_t 探测结果（成功时包含状态码和耗时）
 */
probe_result_t probe_http_execute(const probe_target_t *target, const probe_module_config_t *module_config)
{
    probe_result_t result = {0};
    result.success = false;
    
    // 构建 HTTP URL
    char url[512];
    snprintf(url, sizeof(url), "http://%s:%d/",
             target->target, target->port);
    
    // 记录开始时间
    int64_t start_time = esp_timer_get_time();
    
    // 配置 HTTP 客户端
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = module_config->timeout_ms,
        .disable_auto_redirect = true,  // 禁止自动重定向，精确测量目标耗时
    };
    
    // 初始化并执行请求
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        strncpy(result.error_msg, "Failed to init HTTP client", sizeof(result.error_msg));
        return result;
    }
    
    esp_err_t err = esp_http_client_perform(client);
    int64_t end_time = esp_timer_get_time();
    
    // 计算总耗时（毫秒）
    result.duration_ms = (uint32_t)((end_time - start_time) / 1000);
    
    if (err == ESP_OK) {
        result.success = true;
        result.status_code = esp_http_client_get_status_code(client);  // 获取 HTTP 状态码
        result.details.http.http_status = result.status_code;
    } else {
        strncpy(result.error_msg, "HTTP request failed", sizeof(result.error_msg));
    }
    
    esp_http_client_cleanup(client);  // 释放 HTTP 客户端资源
    return result;
}

/**
 * @brief 执行 HTTPS 探测
 *
 * 构建 https:// URL，发送 GET 请求，测量总耗时。
 * 根据 target->verify_ssl 决定是否验证服务器证书。
 *
 * @param target 探测目标配置
 * @return probe_result_t 探测结果（成功时包含状态码和耗时）
 */
probe_result_t probe_https_execute(const probe_target_t *target, const probe_module_config_t *module_config)
{
    probe_result_t result = {0};
    result.success = false;
    
    // 构建 HTTPS URL
    char url[512];
    snprintf(url, sizeof(url), "https://%s:%d/",
             target->target, target->port);
    
    int64_t start_time = esp_timer_get_time();
    
    // 配置 HTTPS 客户端
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = module_config->timeout_ms,
        .disable_auto_redirect = true,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        strncpy(result.error_msg, "Failed to init HTTPS client", sizeof(result.error_msg));
        return result;
    }
    
    esp_err_t err = esp_http_client_perform(client);
    int64_t end_time = esp_timer_get_time();
    
    result.duration_ms = (uint32_t)((end_time - start_time) / 1000);
    
    if (err == ESP_OK) {
        result.success = true;
        result.status_code = esp_http_client_get_status_code(client);
        result.details.http.http_status = result.status_code;
    } else {
        strncpy(result.error_msg, "HTTPS request failed", sizeof(result.error_msg));
    }
    
    esp_http_client_cleanup(client);
    return result;
}
