/* 
 * WebSocket (WS/WSS) 探测实现
 * 
 * 该模块实现 WebSocket 和安全 WebSocket (WSS) 的探测功能。
 * 包括自定义 Base64 编码器、TCP 连接、WebSocket 握手等。
 * 
 * 功能：
 * 1. Base64 编码器：用于 WebSocket 密钥处理
 * 2. WebSocket 连接：标准 HTTP 升级请求
 * 3. WSS 连接：通过 TLS 传输的 WebSocket 升级
 * 4. 握手协议处理：验证 101 状态码和 "websocket" 字符串
 */

#include "probe_types.h"
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "PROBE_WS";

static int base64_encode(const unsigned char *input, int input_len, char *output, int output_size)
{
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    int i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    while (input_len--) {
        char_array_3[i++] = *(input++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                if (j < output_size - 1) {
                    output[j++] = base64_chars[char_array_4[i]];
                }
            }
            i = 0;
        }
    }
    
    if (i) {
        for (int k = i; k < 3; k++) {
            char_array_3[k] = '\0';
        }
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int k = 0; k < (i + 1); k++) {
            if (j < output_size - 1) {
                output[j++] = base64_chars[char_array_4[k]];
            }
        }
        
        while (i++ < 3) {
            if (j < output_size - 1) {
                output[j++] = '=';
            }
        }
    }
    
    output[j] = '\0';
    return j;
}

probe_result_t probe_ws_execute(const probe_target_t *target, const probe_module_config_t *module_config)
{
    probe_result_t result = {0};
    result.success = false;
    
    struct hostent *host = gethostbyname(target->target);
    if (host == NULL) {
        strncpy(result.error_msg, "DNS resolution failed", sizeof(result.error_msg));
        return result;
    }
    
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
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(target->port);
    dest_addr.sin_addr.s_addr = *((unsigned long *)host->h_addr);
    
    int64_t start_time = esp_timer_get_time();
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    
    if (err != 0) {
        strncpy(result.error_msg, "TCP connection failed", sizeof(result.error_msg));
        close(sock);
        return result;
    }
    
    int64_t connect_done = esp_timer_get_time();
    
    char key[25];
    for (int i = 0; i < 16; i++) {
        key[i] = (char)(esp_random() & 0xFF);
    }
    char key_b64[32];
    base64_encode((unsigned char *)key, 16, key_b64, sizeof(key_b64));
    
    char request[1024];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        "/",
        target->target, target->port,
        key_b64);
    
    send(sock, request, strlen(request), 0);
    
    char response[1024];
    int recv_len = recv(sock, response, sizeof(response) - 1, 0);
    
    int64_t end_time = esp_timer_get_time();
    result.duration_ms = (uint32_t)((end_time - start_time) / 1000);
    
    if (recv_len > 0) {
        response[recv_len] = '\0';
        if (strstr(response, "101") != NULL && strstr(response, "websocket") != NULL) {
            result.success = true;
            result.status_code = 101;
            result.details.ws.connect_time_ms = (uint32_t)((connect_done - start_time) / 1000);
            result.details.ws.handshake_time_ms = (uint32_t)((end_time - connect_done) / 1000);
        } else {
            strncpy(result.error_msg, "WebSocket handshake failed", sizeof(result.error_msg));
        }
    } else {
        strncpy(result.error_msg, "No response from server", sizeof(result.error_msg));
    }
    
    close(sock);
    return result;
}

probe_result_t probe_wss_execute(const probe_target_t *target, const probe_module_config_t *module_config)
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

        result.details.ws.connect_time_ms = (uint32_t)((connect_done - start_time) / 1000);
    }

    /* TLS setup */
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

    /* TLS handshake */
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
        result.details.ws.tls_time_ms = (uint32_t)((tls_done - tls_start) / 1000);
    }

    /* WebSocket upgrade over TLS */
    {
        char key[25];
        for (int i = 0; i < 16; i++) {
            key[i] = (char)(esp_random() & 0xFF);
        }
        char key_b64[32];
        base64_encode((unsigned char *)key, 16, key_b64, sizeof(key_b64));

        char request[1024];
        int req_len = snprintf(request, sizeof(request),
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n",
            "/",
            target->target, target->port,
            key_b64);

        ret = mbedtls_ssl_write(&ssl, (unsigned char *)request, req_len);
        if (ret < 0) {
            snprintf(result.error_msg, sizeof(result.error_msg),
                     "TLS write failed: -0x%04X", (unsigned int)-ret);
            goto cleanup;
        }

        char response[1024];
        int recv_len = mbedtls_ssl_read(&ssl, (unsigned char *)response,
                                        sizeof(response) - 1);

        int64_t end_time = esp_timer_get_time();
        result.duration_ms = (uint32_t)((end_time - start_time) / 1000);

        if (recv_len > 0) {
            response[recv_len] = '\0';
            if (strstr(response, "101") != NULL &&
                strstr(response, "websocket") != NULL) {
                result.success = true;
                result.status_code = 101;
                result.details.ws.handshake_time_ms =
                    result.duration_ms - result.details.ws.connect_time_ms
                                           - result.details.ws.tls_time_ms;

                ESP_LOGI(TAG, "WSS handshake OK: connect=%dms, tls=%dms, ws=%dms, total=%dms",
                         result.details.ws.connect_time_ms,
                         result.details.ws.tls_time_ms,
                         result.details.ws.handshake_time_ms,
                         result.duration_ms);
            } else {
                strncpy(result.error_msg, "WebSocket handshake failed",
                        sizeof(result.error_msg));
            }
        } else {
            snprintf(result.error_msg, sizeof(result.error_msg),
                     "TLS read failed: -0x%04X", (unsigned int)-recv_len);
        }
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
