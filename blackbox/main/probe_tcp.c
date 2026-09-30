/* 
 * TCP and TCP+TLS 探测实现
 * 
 * 该模块提供对目标端点的 TCP 和 TLS 连接探测功能。
 * 包括普通 TCP 连接探测和带 TLS 加密握手验证的 TCP+TLS 探测。
 * 
 * 功能：
 * 1. 创建套接字并设置超时
 * 2. DNS 解析目标主机
 * 3. 测量连接时间
 * 4. 对于 TLS 版本，执行完整的 TLS 握手
 * 
 * 探测结果包括连接时间、TLS 握手时间等指标。
 */

#include "probe_types.h"
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"

static const char *TAG = "PROBE_TCP";

probe_result_t probe_tcp_execute(const probe_target_t *target, const probe_module_config_t *module_config)
{
    probe_result_t result = {0};
    result.success = false;
    
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        strncpy(result.error_msg, "Socket creation failed", sizeof(result.error_msg));
        return result;
    }
    
    struct timeval tv;
    tv.tv_sec = module_config->timeout_ms / 1000;
    tv.tv_usec = (module_config->timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    struct hostent *host = gethostbyname(target->target);
    if (host == NULL) {
        strncpy(result.error_msg, "DNS resolution failed", sizeof(result.error_msg));
        close(sock);
        return result;
    }
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(target->port);
    dest_addr.sin_addr.s_addr = *((unsigned long *)host->h_addr);
    
    int64_t start_time = esp_timer_get_time();
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    int64_t end_time = esp_timer_get_time();
    
    result.duration_ms = (uint32_t)((end_time - start_time) / 1000);
    
    if (err == 0) {
        result.success = true;
        result.status_code = 0;
        result.details.tcp.connect_time_ms = result.duration_ms;
    } else {
        strncpy(result.error_msg, "TCP connection failed", sizeof(result.error_msg));
    }
    
    close(sock);
    return result;
}

probe_result_t probe_tcp_tls_execute(const probe_target_t *target, const probe_module_config_t *module_config)
{
    probe_result_t result = {0};
    result.success = false;

    int64_t start_time = esp_timer_get_time();

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_net_context server_fd;

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_net_init(&server_fd);

    int ret;

    struct hostent *host = gethostbyname(target->target);
    if (host == NULL) {
        strncpy(result.error_msg, "DNS resolution failed", sizeof(result.error_msg));
        goto cleanup;
    }

    {
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            strncpy(result.error_msg, "Socket creation failed", sizeof(result.error_msg));
            goto cleanup;
        }

        server_fd.fd = sock;

        struct timeval tv;
        tv.tv_sec = module_config->timeout_ms / 1000;
        tv.tv_usec = (module_config->timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(target->port);
        dest_addr.sin_addr.s_addr = *((unsigned long *)host->h_addr);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        int64_t connect_done = esp_timer_get_time();

        if (err != 0) {
            strncpy(result.error_msg, "TCP connection failed", sizeof(result.error_msg));
            goto cleanup;
        }

        result.details.tcp.connect_time_ms = (uint32_t)((connect_done - start_time) / 1000);
    }

    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "TLS config failed: -0x%04X", (unsigned int)-ret);
        goto cleanup;
    }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_read_timeout(&conf, module_config->timeout_ms);

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "TLS setup failed: -0x%04X", (unsigned int)-ret);
        goto cleanup;
    }

    mbedtls_ssl_set_hostname(&ssl, target->target);
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send,
                       mbedtls_net_recv, mbedtls_net_recv_timeout);

    {
        int64_t tls_start = esp_timer_get_time();
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                snprintf(result.error_msg, sizeof(result.error_msg),
                         "TLS handshake failed: -0x%04X", (unsigned int)-ret);
                goto cleanup;
            }
        }
        int64_t tls_done = esp_timer_get_time();

        result.success = true;
        result.status_code = 0;
        result.duration_ms = (uint32_t)((tls_done - start_time) / 1000);
        result.details.tcp.tls_time_ms = (uint32_t)((tls_done - tls_start) / 1000);

        ESP_LOGI(TAG, "TLS handshake OK: connect=%dms, tls=%dms, total=%dms",
                 result.details.tcp.connect_time_ms,
                 result.details.tcp.tls_time_ms,
                 result.duration_ms);
    }

cleanup:
    if (!result.success && result.duration_ms == 0) {
        result.duration_ms = (uint32_t)((esp_timer_get_time() - start_time) / 1000);
    }
    if (result.success) {
        mbedtls_ssl_close_notify(&ssl);
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_net_free(&server_fd);

    return result;
}
