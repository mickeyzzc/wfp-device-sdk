/*
 * ICMP ping 探测实现
 *
 * 使用 lwIP 原始套接字发送 ICMP Echo Request 并等待 Echo Reply。
 * 测量往返时间 (RTT)、丢包率等网络质量指标。
 *
 * 功能：
 * 1. 创建 ICMP 原始套接字 (SOCK_RAW, IPPROTO_ICMP)
 * 2. 发送指定数量的 Echo Request 数据包
 * 3. 等待并匹配 Echo Reply
 * 4. 统计 RTT、丢包率等指标
 */

#include "probe_types.h"
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/icmp.h"
#include "lwip/ip4.h"
#include "lwip/inet.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ICMP Echo 类型常量 (lwip/icmp.h 中可能未定义 SOCK_RAW 用法的宏) */
#ifndef ICMP_ECHO
#define ICMP_ECHO       8
#endif
#ifndef ICMP_ECHOREPLY
#define ICMP_ECHOREPLY  0
#endif

/* IP 头部长度 (无选项时的标准长度) */
#define IP_HDR_LEN      20

static const char *TAG = "PROBE_ICMP";

/* 发送缓冲区: ICMP 头 (8 字节) + 最大负载 (默认 56 字节) = 64 字节, 预留余量 */
#define ICMP_BUF_SIZE   128

/*
 * 计算 ICMP 校验和
 * 对 16 位字进行 ones-complement 求和，返回 ones-complement 结果
 */
static uint16_t icmp_checksum(const void *data, size_t len)
{
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    /* 处理奇数字节 */
    if (len == 1) {
        sum += *(const uint8_t *)ptr;
    }

    /* 将 32 位和折叠为 16 位 */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

/*
 * 构建 ICMP Echo Request 数据包
 * 返回数据包总长度
 */
static size_t build_echo_request(char *buf, size_t buf_size,
                                  uint16_t seq, uint16_t id,
                                  uint16_t payload_size, uint8_t pattern)
{
    /* ICMP 头 (8 字节) + 负载 */
    size_t total_len = 8 + payload_size;
    if (total_len > buf_size) {
        total_len = buf_size;
        payload_size = total_len - 8;
    }

    struct icmp_echo_hdr *icmp = (struct icmp_echo_hdr *)buf;
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->chksum = 0;
    icmp->id = id;
    icmp->seqno = htons(seq);

    /* 填充负载区域 */
    if (payload_size > 0) {
        memset(buf + 8, pattern, payload_size);
    }

    /* 计算并填入校验和 */
    icmp->chksum = icmp_checksum(buf, total_len);

    return total_len;
}

probe_result_t probe_icmp_execute(const probe_target_t *target,
                                   const probe_module_config_t *module_config)
{
    probe_result_t result = {0};
    result.success = false;

    /* 提取 ICMP 模块配置 */
    const icmp_module_config_t *icmp_cfg = &module_config->config.icmp;
    uint8_t packet_count = icmp_cfg->packets;
    uint16_t payload_size = icmp_cfg->payload_size;
    uint8_t pattern = icmp_cfg->pattern;

    /* 参数边界保护 */
    if (packet_count == 0) packet_count = 3;
    if (payload_size == 0) payload_size = 56;

    /* 创建 ICMP 原始套接字 */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        strncpy(result.error_msg, "ICMP socket creation failed", sizeof(result.error_msg) - 1);
        ESP_LOGE(TAG, "Failed to create ICMP socket: errno=%d", errno);
        return result;
    }

    /* 设置接收超时 */
    struct timeval tv;
    tv.tv_sec = module_config->timeout_ms / 1000;
    tv.tv_usec = (module_config->timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* DNS 解析目标主机 */
    struct hostent *host = gethostbyname(target->target);
    if (host == NULL) {
        strncpy(result.error_msg, "DNS resolution failed", sizeof(result.error_msg) - 1);
        ESP_LOGE(TAG, "Failed to resolve host: %s", target->target);
        close(sock);
        return result;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = *((uint32_t *)host->h_addr);

    /* 使用任务句柄的低 16 位作为 ICMP 标识符 */
    uint16_t icmp_id = (uint16_t)((uint32_t)xTaskGetCurrentTaskHandle() & 0xFFFF);

    char send_buf[ICMP_BUF_SIZE];
    char recv_buf[ICMP_BUF_SIZE + IP_HDR_LEN];

    uint8_t packets_sent = 0;
    uint8_t packets_received = 0;
    uint32_t total_rtt_ms = 0;

    int64_t probe_start = esp_timer_get_time();

    /* 逐包发送与接收 */
    for (uint8_t seq = 1; seq <= packet_count; seq++) {
        /* 构建 Echo Request */
        size_t pkt_len = build_echo_request(send_buf, sizeof(send_buf),
                                             seq, icmp_id, payload_size, pattern);

        int64_t send_time = esp_timer_get_time();

        /* 发送 ICMP 包 */
        ssize_t sent = sendto(sock, send_buf, pkt_len, 0,
                               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent < 0) {
            ESP_LOGW(TAG, "Failed to send ICMP packet seq=%d: errno=%d", seq, errno);
            continue;
        }
        packets_sent++;

        /* 等待 Echo Reply */
        bool reply_matched = false;
        int max_attempts = 3; /* 最多尝试接收 3 次以过滤无关包 */

        while (max_attempts-- > 0 && !reply_matched) {
            ssize_t recv_len = recv(sock, recv_buf, sizeof(recv_buf), 0);
            if (recv_len < 0) {
                /* 超时或错误，不再等待此包 */
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ESP_LOGW(TAG, "ICMP recv error: errno=%d", errno);
                }
                break;
            }

            int64_t recv_time = esp_timer_get_time();

            /*
             * 解析接收到的数据包
             * SOCK_RAW 在 lwIP 中可能返回包含 IP 头的完整数据包
             * 需要跳过 IP 头部才能拿到 ICMP 内容
             */
            int ip_hdr_len = IP_HDR_LEN;
            if (recv_len >= IP_HDR_LEN) {
                /* 检查 IP 版本和头部长度字段 */
                uint8_t first_byte = (uint8_t)recv_buf[0];
                if ((first_byte >> 4) == 4) {
                    /* IPv4: 低 4 位表示头部长度 (以 4 字节为单位) */
                    ip_hdr_len = (first_byte & 0x0F) * 4;
                }
            }

            /* 确保 ICMP 载荷足够长 */
            if (recv_len < ip_hdr_len + (ssize_t)sizeof(struct icmp_echo_hdr)) {
                continue;
            }

            struct icmp_echo_hdr *reply = (struct icmp_echo_hdr *)(recv_buf + ip_hdr_len);

            /* 验证是 Echo Reply 且匹配我们的 id 和 seq */
            if (reply->type == ICMP_ECHOREPLY &&
                reply->id == icmp_id &&
                ntohs(reply->seqno) == seq) {

                uint32_t rtt_ms = (uint32_t)((recv_time - send_time) / 1000);
                total_rtt_ms += rtt_ms;
                packets_received++;
                reply_matched = true;

                ESP_LOGD(TAG, "ICMP reply seq=%d rtt=%dms", seq, rtt_ms);
            }
            /* 不是我们期望的 reply，继续等待 */
        }
    }

    int64_t probe_end = esp_timer_get_time();
    result.duration_ms = (uint32_t)((probe_end - probe_start) / 1000);

    /* 填充结果 */
    result.details.icmp.packets_sent = packets_sent;
    result.details.icmp.packets_received = packets_received;

    if (packets_received > 0) {
        result.success = true;
        result.status_code = packets_received;
        result.details.icmp.rtt_ms = total_rtt_ms / packets_received;
    } else {
        result.success = false;
        result.status_code = 0;
        result.details.icmp.rtt_ms = 0;
        if (packets_sent == 0) {
            strncpy(result.error_msg, "Failed to send any ICMP packets",
                    sizeof(result.error_msg) - 1);
        } else {
            strncpy(result.error_msg, "No ICMP replies received (timeout)",
                    sizeof(result.error_msg) - 1);
        }
    }

    close(sock);

    ESP_LOGI(TAG, "ICMP ping %s: success=%d, RTT=%dms, sent=%d, recv=%d",
             target->target, result.success, result.details.icmp.rtt_ms,
             packets_sent, packets_received);

    return result;
}
