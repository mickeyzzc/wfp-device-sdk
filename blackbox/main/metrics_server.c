/*
 * Prometheus 指标 HTTP 服务器实现 (esp_http_server)
 *
 * 基于 ESP-IDF esp_http_server 组件，替代原始 LWIP socket 实现。
 * 兼容 blackbox_exporter 多模块探测模式。
 *
 * 端点:
 *   GET  /metrics                - 所有目标聚合的 Prometheus 指标
 *   GET  /probe?target=X&module=Y - 单次同步探测，返回 Prometheus 指标
 *   GET  /config                 - 当前配置 JSON (模块 + 目标 + 全局参数)
 *   POST /reload                 - 触发 SPIFFS 配置热加载
 */

#include "metrics_server.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "probe_manager.h"
#include "config_manager.h"

static const char *TAG = "METRICS";
static httpd_handle_t s_server = NULL;

#define METRICS_BUFFER_SIZE  8192
#define QUERY_BUF_SIZE       256

/* Prometheus exposition 格式内容类型 */
#define PROMETHEUS_CT  "text/plain; version=0.0.4; charset=utf-8"

/* ---- 辅助函数 ---- */

/**
 * 模块类型 → 字符串 (仅用于 /config JSON 序列化)
 */
static const char *module_type_str(probe_module_type_t type)
{
    switch (type) {
        case MODULE_HTTP:     return "http";
        case MODULE_HTTPS:    return "https";
        case MODULE_TCP:      return "tcp";
        case MODULE_TCP_TLS:  return "tcp_tls";
        case MODULE_DNS:      return "dns";
        case MODULE_ICMP:     return "icmp";
        case MODULE_WS:       return "ws";
        case MODULE_WSS:      return "wss";
        default:              return "unknown";
    }
}

/**
 * 格式化追加一行到缓冲区，返回新偏移量。
 * 缓冲区满时安全截断，不会越界写入。
 */
static int append_line(char *buf, int offset, int max_size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + offset, max_size - offset, fmt, ap);
    va_end(ap);
    if (n < 0 || offset + n >= max_size) {
        return offset;  /* 截断: 不越界 */
    }
    return offset + n;
}

/**
 * 将单个目标的 Prometheus 指标写入缓冲区。
 * 通用指标: probe_success, probe_duration_seconds, probe_http_status_code, probe_ip_protocol
 * ICMP 模块额外输出: probe_icmp_rtt_ms, probe_icmp_packets_sent, probe_icmp_packets_received
 */
static int write_target_metrics(char *buf, int offset, int max_size,
                                const probe_target_t *target,
                                const probe_result_t *result,
                                const probe_module_t *module)
{
    const char *t = target->target;
    const char *m = target->module_name;

    /* 通用指标 */
    offset = append_line(buf, offset, max_size,
        "probe_success{target=\"%s\",module=\"%s\"} %d\n",
        t, m, result->success ? 1 : 0);

    offset = append_line(buf, offset, max_size,
        "probe_duration_seconds{target=\"%s\",module=\"%s\"} %.3f\n",
        t, m, result->duration_ms / 1000.0f);

    offset = append_line(buf, offset, max_size,
        "probe_http_status_code{target=\"%s\",module=\"%s\"} %d\n",
        t, m, result->status_code);

    offset = append_line(buf, offset, max_size,
        "probe_ip_protocol{target=\"%s\",module=\"%s\"} 4\n",
        t, m);

    /* ICMP 专用指标 */
    if (module && module->config.type == MODULE_ICMP) {
        offset = append_line(buf, offset, max_size,
            "probe_icmp_rtt_ms{target=\"%s\",module=\"%s\"} %lu\n",
            t, m, (unsigned long)result->details.icmp.rtt_ms);
        offset = append_line(buf, offset, max_size,
            "probe_icmp_packets_sent{target=\"%s\",module=\"%s\"} %d\n",
            t, m, result->details.icmp.packets_sent);
        offset = append_line(buf, offset, max_size,
            "probe_icmp_packets_received{target=\"%s\",module=\"%s\"} %d\n",
            t, m, result->details.icmp.packets_received);
    }

    return offset;
}

/**
 * 写入 Prometheus HELP/TYPE 头部 (通用指标)
 */
static int write_common_headers(char *buf, int offset, int max_size)
{
    return append_line(buf, offset, max_size,
        "# HELP probe_success Whether the probe succeeded\n"
        "# TYPE probe_success gauge\n"
        "# HELP probe_duration_seconds Duration of the probe in seconds\n"
        "# TYPE probe_duration_seconds gauge\n"
        "# HELP probe_http_status_code HTTP status code\n"
        "# TYPE probe_http_status_code gauge\n"
        "# HELP probe_ip_protocol IP protocol version\n"
        "# TYPE probe_ip_protocol gauge\n");
}

/**
 * 写入 ICMP HELP/TYPE 头部
 */
static int write_icmp_headers(char *buf, int offset, int max_size)
{
    return append_line(buf, offset, max_size,
        "# HELP probe_icmp_rtt_ms ICMP round trip time in ms\n"
        "# TYPE probe_icmp_rtt_ms gauge\n"
        "# HELP probe_icmp_packets_sent ICMP packets sent\n"
        "# TYPE probe_icmp_packets_sent gauge\n"
        "# HELP probe_icmp_packets_received ICMP packets received\n"
        "# TYPE probe_icmp_packets_received gauge\n");
}

/* ---- 端点处理函数 ---- */

/**
 * GET /metrics - 所有目标聚合 Prometheus 指标
 */
static esp_err_t metrics_handler(httpd_req_t *req)
{
    char buf[METRICS_BUFFER_SIZE];
    int offset = 0;

    /* HELP/TYPE 头部 */
    offset = write_common_headers(buf, offset, sizeof(buf));
    offset = write_icmp_headers(buf, offset, sizeof(buf));

    /* 获取目标和探测结果 */
    const probe_target_t *targets;
    uint8_t target_count;
    config_get_targets(&targets, &target_count);

    uint8_t result_count;
    const probe_result_t *results = probe_manager_get_results(&result_count);

    /* 取较小值防止数组越界 */
    uint8_t count = target_count < result_count ? target_count : result_count;

    for (int i = 0; i < count; i++) {
        const probe_module_t *mod = config_get_module_by_name(targets[i].module_name);
        offset = write_target_metrics(buf, offset, sizeof(buf),
                                      &targets[i], &results[i], mod);
    }

    httpd_resp_set_type(req, PROMETHEUS_CT);
    return httpd_resp_send(req, buf, offset);
}

/**
 * GET /probe?target=X&module=Y[&port=P] - 单次同步探测 (blackbox_exporter 兼容)
 *
 * target 参数: 目标主机名/IP (任意值，不需要预配置)
 * module 参数: 模块名称 (对应 module->name，必须预配置)
 * port 参数: 可选端口，默认 0 (由模块决定默认端口)
 */
static esp_err_t probe_handler(httpd_req_t *req)
{
    char query[QUERY_BUF_SIZE] = {0};
    char target_val[128] = {0};
    char module_val[32] = {0};
    char port_val[8] = {0};

    /* 解析 URL 查询参数 */
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query parameters");
        return ESP_FAIL;
    }

    bool has_target = (httpd_query_key_value(query, "target", target_val, sizeof(target_val)) == ESP_OK);
    bool has_module = (httpd_query_key_value(query, "module", module_val, sizeof(module_val)) == ESP_OK);

    if (!has_target || target_val[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'target' parameter");
        return ESP_FAIL;
    }

    /* 查找模块配置 */
    const probe_module_t *module = NULL;
    if (has_module && module_val[0] != '\0') {
        module = config_get_module_by_name(module_val);
    }
    if (!module) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Module not found");
        return ESP_FAIL;
    }

    /* 解析可选端口参数 */
    uint16_t port = 0;
    if (httpd_query_key_value(query, "port", port_val, sizeof(port_val)) == ESP_OK && port_val[0] != '\0') {
        int p = atoi(port_val);
        if (p > 0 && p <= 65535) {
            port = (uint16_t)p;
        }
    }

    /* 对任意主机执行探测 (不需要预配置目标) */
    probe_result_t result = probe_manager_probe_host(target_val, port, module_val);

    /* 构造临时 target 用于标签生成 */
    probe_target_t tmp = {0};
    strncpy(tmp.target, target_val, sizeof(tmp.target) - 1);
    tmp.port = port;
    strncpy(tmp.module_name, module_val, sizeof(tmp.module_name) - 1);

    /* 生成 Prometheus 指标 */
    char buf[2048];
    int offset = 0;

    offset = write_common_headers(buf, offset, sizeof(buf));
    if (module->config.type == MODULE_ICMP) {
        offset = write_icmp_headers(buf, offset, sizeof(buf));
    }
    offset = write_target_metrics(buf, offset, sizeof(buf), &tmp, &result, module);

    httpd_resp_set_type(req, PROMETHEUS_CT);
    return httpd_resp_send(req, buf, offset);
}

/**
 * GET /config - 当前配置 JSON
 */
static esp_err_t config_handler(httpd_req_t *req)
{
    const blackbox_config_t *cfg = config_get_config();
    if (!cfg) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config not available");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();

    /* 模块数组 */
    cJSON *j_modules = cJSON_CreateArray();
    for (int i = 0; i < cfg->module_count; i++) {
        const probe_module_t *mod = &cfg->modules[i];
        cJSON *jm = cJSON_CreateObject();
        cJSON_AddStringToObject(jm, "name", mod->name);
        cJSON_AddStringToObject(jm, "type", module_type_str(mod->config.type));
        cJSON_AddNumberToObject(jm, "timeout_ms", mod->config.timeout_ms);
        cJSON_AddItemToArray(j_modules, jm);
    }
    cJSON_AddItemToObject(root, "modules", j_modules);

    /* 目标数组 */
    cJSON *j_targets = cJSON_CreateArray();
    for (int i = 0; i < cfg->target_count; i++) {
        const probe_target_t *tgt = &cfg->targets[i];
        cJSON *jt = cJSON_CreateObject();
        cJSON_AddStringToObject(jt, "name", tgt->name);
        cJSON_AddStringToObject(jt, "target", tgt->target);
        cJSON_AddNumberToObject(jt, "port", tgt->port);
        cJSON_AddNumberToObject(jt, "interval_ms", tgt->interval_ms);
        cJSON_AddStringToObject(jt, "module_name", tgt->module_name);
        cJSON_AddItemToArray(j_targets, jt);
    }
    cJSON_AddItemToObject(root, "targets", j_targets);

    /* 全局参数 */
    cJSON_AddNumberToObject(root, "scrape_interval_ms", cfg->scrape_interval_ms);
    cJSON_AddNumberToObject(root, "metrics_port", cfg->metrics_port);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * POST /reload - 触发配置热加载
 */
static esp_err_t reload_handler(httpd_req_t *req)
{
    esp_err_t err = config_reload();

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config reload failed");
        return ESP_FAIL;
    }

    const char resp[] = "{\"status\":\"ok\",\"message\":\"Config reloaded\"}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, strlen(resp));
}

/* ---- 公共 API ---- */

esp_err_t metrics_server_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    const blackbox_config_t *cfg = config_get_config();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = cfg->metrics_port;
    config.stack_size = 16384;
    config.max_uri_handlers = 4;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start metrics server");
        return ESP_FAIL;
    }

    /* 注册四个端点处理函数 */
    const httpd_uri_t uris[] = {
        { .uri = "/metrics", .method = HTTP_GET,  .handler = metrics_handler, .user_ctx = NULL },
        { .uri = "/probe",   .method = HTTP_GET,  .handler = probe_handler,   .user_ctx = NULL },
        { .uri = "/config",  .method = HTTP_GET,  .handler = config_handler,  .user_ctx = NULL },
        { .uri = "/reload",  .method = HTTP_POST, .handler = reload_handler,  .user_ctx = NULL },
    };
    for (int i = 0; i < 4; i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    ESP_LOGI(TAG, "Metrics server started on port %d", config.server_port);
    return ESP_OK;
}
