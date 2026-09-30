/*
 * metrics_server.h - Prometheus 指标服务器接口
 *
 * 基于 esp_http_server 的 Prometheus 格式指标服务器。
 * 兼容 blackbox_exporter 探测模式。
 *
 * 端点:
 *   GET  /metrics                - 所有目标聚合指标
 *   GET  /probe?target=X&module=Y - 单次探测
 *   GET  /config                 - 当前 JSON 配置
 *   POST /reload                 - 触发配置热加载
 */

#ifndef METRICS_SERVER_H
#define METRICS_SERVER_H

#include "esp_err.h"

/**
 * @brief 启动 Prometheus 指标服务器
 *
 * 在配置的端口 (config_get_config()->metrics_port) 上启动 HTTP 服务器，
 * 注册 /metrics、/probe、/config、/reload 四个端点处理函数。
 *
 * @return ESP_OK 启动成功, ESP_FAIL 启动失败
 */
esp_err_t metrics_server_start(void);

#endif
