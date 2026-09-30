/*
 * config_manager.c - 配置管理器实现
 *
 * 负责管理探测配置，包括:
 * 1. SPIFFS 文件系统挂载和 JSON 配置读写
 * 2. 工厂默认配置生成
 * 3. 配置验证和热加载
 * 4. NVS WiFi 凭证存储 (保持不变)
 */

#include "config_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "CONFIG";

/* ---- 静态配置存储 ---- */

static probe_module_t s_modules[MAX_MODULES];
static probe_target_t s_targets[MAX_TARGETS];
static uint8_t s_module_count = 0;
static uint8_t s_target_count = 0;
static uint8_t s_config_version = 0;

static const char *s_config_file_path = "/spiffs/blackbox.json";

/* 兼容旧接口的配置结构体 */
static blackbox_config_t s_config;

/* ---- 内部辅助函数 ---- */

/* prober 字符串转模块类型枚举 */
static probe_module_type_t prober_str_to_type(const char *prober)
{
    if (strcmp(prober, "http") == 0)     return MODULE_HTTP;
    if (strcmp(prober, "https") == 0)    return MODULE_HTTPS;
    if (strcmp(prober, "tcp") == 0)      return MODULE_TCP;
    if (strcmp(prober, "tcp_tls") == 0)  return MODULE_TCP_TLS;
    if (strcmp(prober, "dns") == 0)      return MODULE_DNS;
    if (strcmp(prober, "icmp") == 0)     return MODULE_ICMP;
    if (strcmp(prober, "ws") == 0)       return MODULE_WS;
    if (strcmp(prober, "wss") == 0)      return MODULE_WSS;
    return MODULE_HTTP; /* 默认 HTTP */
}

/* 模块类型枚举转 prober 字符串 */
static const char* prober_type_to_str(probe_module_type_t type)
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
        default:              return "http";
    }
}

/* 更新 s_config 结构体中的指针和计数 */
static void update_config_ptr(void)
{
    s_config.modules = s_modules;
    s_config.module_count = s_module_count;
    s_config.targets = s_targets;
    s_config.target_count = s_target_count;
}

/* ---- SPIFFS 挂载 ---- */

static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "SPIFFS 挂载或格式化失败");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "未找到 SPIFFS 分区");
        } else {
            ESP_LOGE(TAG, "SPIFFS 初始化失败 (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取 SPIFFS 分区信息失败 (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS: 总空间=%d 字节, 已用=%d 字节", (int)total, (int)used);
    }
    return ESP_OK;
}

/* ---- JSON 解析 ---- */

/* 解析 HTTP 模块配置 */
static void parse_http_config(const cJSON *http_obj, http_module_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->method, "GET", sizeof(cfg->method) - 1);

    const cJSON *method = cJSON_GetObjectItem(http_obj, "method");
    if (method && cJSON_IsString(method)) {
        strncpy(cfg->method, method->valuestring, sizeof(cfg->method) - 1);
    }

    const cJSON *codes = cJSON_GetObjectItem(http_obj, "valid_status_codes");
    if (codes && cJSON_IsArray(codes)) {
        int count = cJSON_GetArraySize(codes);
        if (count > 8) count = 8;
        cfg->valid_status_count = (uint8_t)count;
        for (int i = 0; i < count; i++) {
            const cJSON *code = cJSON_GetArrayItem(codes, i);
            if (code && cJSON_IsNumber(code)) {
                cfg->valid_status_codes[i] = (uint16_t)code->valueint;
            }
        }
    } else {
        cfg->valid_status_codes[0] = 200;
        cfg->valid_status_count = 1;
    }

    const cJSON *no_follow = cJSON_GetObjectItem(http_obj, "no_follow_redirects");
    if (no_follow && cJSON_IsBool(no_follow)) {
        cfg->no_follow_redirects = cJSON_IsTrue(no_follow);
    }
}

/* 解析 TCP 模块配置 */
static void parse_tcp_config(const cJSON *tcp_obj, tcp_module_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    const cJSON *tls = cJSON_GetObjectItem(tcp_obj, "tls");
    if (tls && cJSON_IsBool(tls)) {
        cfg->tls = cJSON_IsTrue(tls);
    }

    const cJSON *query = cJSON_GetObjectItem(tcp_obj, "query");
    if (query && cJSON_IsString(query)) {
        strncpy(cfg->query, query->valuestring, sizeof(cfg->query) - 1);
    }

    const cJSON *resp = cJSON_GetObjectItem(tcp_obj, "expected_response");
    if (resp && cJSON_IsString(resp)) {
        strncpy(cfg->expected_response, resp->valuestring, sizeof(cfg->expected_response) - 1);
    }
}

/* 解析 DNS 模块配置 */
static void parse_dns_config(const cJSON *dns_obj, dns_module_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->query_name, "dns.google", sizeof(cfg->query_name) - 1);
    cfg->query_type = 1;

    const cJSON *qname = cJSON_GetObjectItem(dns_obj, "query_name");
    if (qname && cJSON_IsString(qname)) {
        strncpy(cfg->query_name, qname->valuestring, sizeof(cfg->query_name) - 1);
    }

    const cJSON *qtype = cJSON_GetObjectItem(dns_obj, "query_type");
    if (qtype && cJSON_IsNumber(qtype)) {
        cfg->query_type = (uint8_t)qtype->valueint;
    }
}

/* 解析 ICMP 模块配置 */
static void parse_icmp_config(const cJSON *icmp_obj, icmp_module_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->packets = 3;
    cfg->payload_size = 56;

    const cJSON *packets = cJSON_GetObjectItem(icmp_obj, "packets");
    if (packets && cJSON_IsNumber(packets)) {
        cfg->packets = (uint8_t)packets->valueint;
    }

    const cJSON *size = cJSON_GetObjectItem(icmp_obj, "payload_size");
    if (size && cJSON_IsNumber(size)) {
        cfg->payload_size = (uint16_t)size->valueint;
    }

    const cJSON *pattern = cJSON_GetObjectItem(icmp_obj, "pattern");
    if (pattern && cJSON_IsNumber(pattern)) {
        cfg->pattern = (uint8_t)pattern->valueint;
    }
}

/* 解析单个模块 JSON 对象 */
static bool parse_module_json(const char *name, const cJSON *module_obj, probe_module_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->name, name, sizeof(out->name) - 1);

    const cJSON *prober = cJSON_GetObjectItem(module_obj, "prober");
    if (!prober || !cJSON_IsString(prober)) {
        ESP_LOGE(TAG, "模块 '%s' 缺少 prober 字段", name);
        return false;
    }
    out->config.type = prober_str_to_type(prober->valuestring);

    const cJSON *timeout = cJSON_GetObjectItem(module_obj, "timeout");
    if (timeout && cJSON_IsNumber(timeout)) {
        out->config.timeout_ms = (uint32_t)(timeout->valueint * 1000);
    } else {
        out->config.timeout_ms = 5000;
    }

    /* 根据模块类型解析特定配置 */
    switch (out->config.type) {
        case MODULE_HTTP:
        case MODULE_HTTPS: {
            const cJSON *http = cJSON_GetObjectItem(module_obj, "http");
            if (http) {
                parse_http_config(http, &out->config.config.http);
            } else {
                memset(&out->config.config.http, 0, sizeof(http_module_config_t));
                strncpy(out->config.config.http.method, "GET",
                        sizeof(out->config.config.http.method) - 1);
                out->config.config.http.valid_status_codes[0] = 200;
                out->config.config.http.valid_status_count = 1;
            }
            break;
        }
        case MODULE_TCP:
        case MODULE_TCP_TLS: {
            const cJSON *tcp = cJSON_GetObjectItem(module_obj, "tcp");
            if (tcp) {
                parse_tcp_config(tcp, &out->config.config.tcp);
            }
            break;
        }
        case MODULE_DNS: {
            const cJSON *dns = cJSON_GetObjectItem(module_obj, "dns");
            if (dns) {
                parse_dns_config(dns, &out->config.config.dns);
            }
            break;
        }
        case MODULE_ICMP: {
            const cJSON *icmp = cJSON_GetObjectItem(module_obj, "icmp");
            if (icmp) {
                parse_icmp_config(icmp, &out->config.config.icmp);
            }
            break;
        }
        case MODULE_WS:
        case MODULE_WSS: {
            const cJSON *http = cJSON_GetObjectItem(module_obj, "http");
            if (http) {
                parse_http_config(http, &out->config.config.http);
            }
            break;
        }
    }

    return true;
}

/* 解析完整 JSON 配置字符串到 s_modules/s_targets */
static esp_err_t config_parse_json(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *err_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON 解析失败: %s", err_ptr ? err_ptr : "未知错误");
        return ESP_FAIL;
    }

    uint8_t mod_count = 0;
    uint8_t tgt_count = 0;

    /* 使用静态临时数组（避免栈溢出，config_parse_json 是单线程的） */
    static probe_module_t tmp_modules[MAX_MODULES];
    static probe_target_t tmp_targets[MAX_TARGETS];

    /* 解析 modules 对象 */
    const cJSON *modules_obj = cJSON_GetObjectItem(root, "modules");
    if (modules_obj && cJSON_IsObject(modules_obj)) {
        cJSON *mod_item = NULL;
        cJSON_ArrayForEach(mod_item, modules_obj) {
            if (mod_count >= MAX_MODULES) {
                ESP_LOGW(TAG, "模块数量超过上限 %d，截断", MAX_MODULES);
                break;
            }
            if (!cJSON_IsObject(mod_item)) continue;
            if (!parse_module_json(mod_item->string, mod_item, &tmp_modules[mod_count])) {
                ESP_LOGW(TAG, "跳过无效模块: %s", mod_item->string);
                continue;
            }
            mod_count++;
        }
    } else {
        ESP_LOGW(TAG, "JSON 配置中未找到 'modules' 对象");
    }

    /* 解析 targets 数组 */
    const cJSON *targets_arr = cJSON_GetObjectItem(root, "targets");
    if (targets_arr && cJSON_IsArray(targets_arr)) {
        int arr_size = cJSON_GetArraySize(targets_arr);
        for (int i = 0; i < arr_size && tgt_count < MAX_TARGETS; i++) {
            const cJSON *tgt = cJSON_GetArrayItem(targets_arr, i);
            if (!cJSON_IsObject(tgt)) continue;

            probe_target_t *t = &tmp_targets[tgt_count];
            memset(t, 0, sizeof(*t));

            const cJSON *name = cJSON_GetObjectItem(tgt, "name");
            if (name && cJSON_IsString(name)) {
                strncpy(t->name, name->valuestring, sizeof(t->name) - 1);
            }

            const cJSON *target = cJSON_GetObjectItem(tgt, "target");
            if (target && cJSON_IsString(target)) {
                strncpy(t->target, target->valuestring, sizeof(t->target) - 1);
            }

            const cJSON *port = cJSON_GetObjectItem(tgt, "port");
            if (port && cJSON_IsNumber(port)) {
                t->port = (uint16_t)port->valueint;
            }

            const cJSON *interval = cJSON_GetObjectItem(tgt, "interval");
            if (interval && cJSON_IsNumber(interval)) {
                t->interval_ms = (uint32_t)(interval->valueint * 1000);
            }

            const cJSON *module = cJSON_GetObjectItem(tgt, "module");
            if (module && cJSON_IsString(module)) {
                strncpy(t->module_name, module->valuestring, sizeof(t->module_name) - 1);
            }

            tgt_count++;
        }
    } else {
        ESP_LOGW(TAG, "JSON 配置中未找到 'targets' 数组");
    }

    /* 解析全局配置 */
    const cJSON *scrape_interval = cJSON_GetObjectItem(root, "scrape_interval");
    if (scrape_interval && cJSON_IsNumber(scrape_interval)) {
        s_config.scrape_interval_ms = (uint32_t)(scrape_interval->valueint * 1000);
    } else {
        s_config.scrape_interval_ms = 30000;
    }

    const cJSON *metrics_port = cJSON_GetObjectItem(root, "metrics_port");
    if (metrics_port && cJSON_IsNumber(metrics_port)) {
        s_config.metrics_port = (uint16_t)metrics_port->valueint;
    } else {
        s_config.metrics_port = 9090;
    }

    cJSON_Delete(root);

    /* 对没有设置 interval_ms 的目标使用全局 scrape_interval */
    for (int i = 0; i < tgt_count; i++) {
        if (tmp_targets[i].interval_ms == 0) {
            tmp_targets[i].interval_ms = s_config.scrape_interval_ms;
        }
    }

    /* 解析成功，提交到静态数组 */
    memcpy(s_modules, tmp_modules, sizeof(s_modules));
    memcpy(s_targets, tmp_targets, sizeof(s_targets));
    s_module_count = mod_count;
    s_target_count = tgt_count;
    update_config_ptr();

    ESP_LOGI(TAG, "JSON 解析完成: %d 模块, %d 目标", s_module_count, s_target_count);
    return ESP_OK;
}

/* ---- SPIFFS 文件读写 ---- */

/* 从 SPIFFS 加载配置文件 */
static esp_err_t config_load_from_spiffs(void)
{
    FILE *f = fopen(s_config_file_path, "r");
    if (!f) {
        ESP_LOGW(TAG, "配置文件未找到: %s", s_config_file_path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > 65536) {
        ESP_LOGE(TAG, "配置文件大小异常: %ld 字节", len);
        fclose(f);
        return ESP_FAIL;
    }

    char *json_str = malloc(len + 1);
    if (!json_str) {
        ESP_LOGE(TAG, "分配 JSON 缓冲区失败 (%ld 字节)", len);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(json_str, 1, len, f);
    json_str[read_len] = '\0';
    fclose(f);

    ESP_LOGI(TAG, "从 SPIFFS 读取配置: %d 字节", (int)read_len);
    esp_err_t ret = config_parse_json(json_str);
    free(json_str);
    return ret;
}

/* 序列化单个模块配置到 cJSON 对象 */
static cJSON* serialize_module(const probe_module_t *mod)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "prober", prober_type_to_str(mod->config.type));
    cJSON_AddNumberToObject(obj, "timeout", mod->config.timeout_ms / 1000);

    switch (mod->config.type) {
        case MODULE_HTTP:
        case MODULE_HTTPS: {
            cJSON *http = cJSON_CreateObject();
            cJSON_AddStringToObject(http, "method", mod->config.config.http.method);
            cJSON *codes = cJSON_CreateArray();
            for (int i = 0; i < mod->config.config.http.valid_status_count; i++) {
                cJSON_AddItemToArray(codes, cJSON_CreateNumber(
                    mod->config.config.http.valid_status_codes[i]));
            }
            cJSON_AddItemToObject(http, "valid_status_codes", codes);
            cJSON_AddBoolToObject(http, "no_follow_redirects",
                                  mod->config.config.http.no_follow_redirects);
            cJSON_AddItemToObject(obj, "http", http);
            break;
        }
        case MODULE_TCP:
        case MODULE_TCP_TLS: {
            cJSON *tcp = cJSON_CreateObject();
            cJSON_AddBoolToObject(tcp, "tls", mod->config.config.tcp.tls);
            cJSON_AddStringToObject(tcp, "query", mod->config.config.tcp.query);
            cJSON_AddStringToObject(tcp, "expected_response",
                                    mod->config.config.tcp.expected_response);
            cJSON_AddItemToObject(obj, "tcp", tcp);
            break;
        }
        case MODULE_DNS: {
            cJSON *dns = cJSON_CreateObject();
            cJSON_AddStringToObject(dns, "query_name", mod->config.config.dns.query_name);
            cJSON_AddNumberToObject(dns, "query_type", mod->config.config.dns.query_type);
            cJSON_AddItemToObject(obj, "dns", dns);
            break;
        }
        case MODULE_ICMP: {
            cJSON *icmp = cJSON_CreateObject();
            cJSON_AddNumberToObject(icmp, "packets", mod->config.config.icmp.packets);
            cJSON_AddNumberToObject(icmp, "payload_size", mod->config.config.icmp.payload_size);
            cJSON_AddNumberToObject(icmp, "pattern", mod->config.config.icmp.pattern);
            cJSON_AddItemToObject(obj, "icmp", icmp);
            break;
        }
        case MODULE_WS:
        case MODULE_WSS: {
            cJSON *http = cJSON_CreateObject();
            cJSON_AddStringToObject(http, "method", mod->config.config.http.method);
            cJSON_AddBoolToObject(http, "no_follow_redirects",
                                  mod->config.config.http.no_follow_redirects);
            cJSON_AddItemToObject(obj, "http", http);
            break;
        }
    }

    return obj;
}

/* 序列化当前配置到 JSON 并写入 SPIFFS */
static esp_err_t config_save_to_spiffs(void)
{
    cJSON *root = cJSON_CreateObject();

    /* 序列化模块 */
    cJSON *modules = cJSON_CreateObject();
    for (int i = 0; i < s_module_count; i++) {
        cJSON *mod_obj = serialize_module(&s_modules[i]);
        cJSON_AddItemToObject(modules, s_modules[i].name, mod_obj);
    }
    cJSON_AddItemToObject(root, "modules", modules);

    /* 序列化目标 */
    cJSON *targets = cJSON_CreateArray();
    for (int i = 0; i < s_target_count; i++) {
        cJSON *tgt = cJSON_CreateObject();
        cJSON_AddStringToObject(tgt, "name", s_targets[i].name);
        cJSON_AddStringToObject(tgt, "target", s_targets[i].target);
        cJSON_AddNumberToObject(tgt, "port", s_targets[i].port);
        if (s_targets[i].interval_ms > 0) {
            cJSON_AddNumberToObject(tgt, "interval", s_targets[i].interval_ms / 1000);
        }
        cJSON_AddStringToObject(tgt, "module", s_targets[i].module_name);
        cJSON_AddItemToArray(targets, tgt);
    }
    cJSON_AddItemToObject(root, "targets", targets);

    /* 全局配置 */
    cJSON_AddNumberToObject(root, "scrape_interval", s_config.scrape_interval_ms / 1000);
    cJSON_AddNumberToObject(root, "metrics_port", s_config.metrics_port);

    /* 序列化为格式化 JSON 字符串 */
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "JSON 序列化失败");
        return ESP_FAIL;
    }

    /* 写入文件 */
    FILE *f = fopen(s_config_file_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "无法打开配置文件写入: %s", s_config_file_path);
        cJSON_free(json_str);
        return ESP_FAIL;
    }

    size_t json_len = strlen(json_str);
    size_t written = fwrite(json_str, 1, json_len, f);
    fclose(f);
    cJSON_free(json_str);

    if (written != json_len) {
        ESP_LOGE(TAG, "写入配置文件不完整: 期望 %d, 实际 %d", (int)json_len, (int)written);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "配置已保存到 SPIFFS: %s (%d 字节)", s_config_file_path, (int)json_len);
    return ESP_OK;
}

/* ---- 工厂默认配置 ---- */

static esp_err_t config_get_factory_defaults(void)
{
    ESP_LOGI(TAG, "加载工厂默认配置");

    s_module_count = 0;
    s_target_count = 0;
    memset(s_modules, 0, sizeof(s_modules));
    memset(s_targets, 0, sizeof(s_targets));

    /* http_2xx 模块 */
    probe_module_t *m0 = &s_modules[s_module_count++];
    strncpy(m0->name, "http_2xx", sizeof(m0->name) - 1);
    m0->config.type = MODULE_HTTP;
    m0->config.timeout_ms = 10000;
    strncpy(m0->config.config.http.method, "GET", sizeof(m0->config.config.http.method) - 1);
    m0->config.config.http.valid_status_codes[0] = 200;
    m0->config.config.http.valid_status_count = 1;
    m0->config.config.http.no_follow_redirects = false;

    /* tcp_connect 模块 */
    probe_module_t *m1 = &s_modules[s_module_count++];
    strncpy(m1->name, "tcp_connect", sizeof(m1->name) - 1);
    m1->config.type = MODULE_TCP;
    m1->config.timeout_ms = 5000;
    m1->config.config.tcp.tls = false;

    /* dns_resolve 模块 */
    probe_module_t *m2 = &s_modules[s_module_count++];
    strncpy(m2->name, "dns_resolve", sizeof(m2->name) - 1);
    m2->config.type = MODULE_DNS;
    m2->config.timeout_ms = 5000;
    strncpy(m2->config.config.dns.query_name, "dns.google",
            sizeof(m2->config.config.dns.query_name) - 1);
    m2->config.config.dns.query_type = 1;

    /* icmp_ping 模块 */
    probe_module_t *m3 = &s_modules[s_module_count++];
    strncpy(m3->name, "icmp_ping", sizeof(m3->name) - 1);
    m3->config.type = MODULE_ICMP;
    m3->config.timeout_ms = 5000;
    m3->config.config.icmp.packets = 3;
    m3->config.config.icmp.payload_size = 56;
    m3->config.config.icmp.pattern = 0;

    /* google_http 目标 */
    probe_target_t *t0 = &s_targets[s_target_count++];
    strncpy(t0->name, "google_http", sizeof(t0->name) - 1);
    strncpy(t0->target, "httpbin.org", sizeof(t0->target) - 1);
    t0->port = 80;
    t0->interval_ms = 30000;
    strncpy(t0->module_name, "http_2xx", sizeof(t0->module_name) - 1);

    /* google_dns 目标 */
    probe_target_t *t1 = &s_targets[s_target_count++];
    strncpy(t1->name, "google_dns", sizeof(t1->name) - 1);
    strncpy(t1->target, "8.8.8.8", sizeof(t1->target) - 1);
    t1->port = 53;
    t1->interval_ms = 30000;
    strncpy(t1->module_name, "dns_resolve", sizeof(t1->module_name) - 1);

    /* 全局配置 */
    s_config.scrape_interval_ms = 30000;
    s_config.metrics_port = 9090;

    update_config_ptr();
    return ESP_OK;
}

/* ---- 配置验证 ---- */

static esp_err_t config_validate(const blackbox_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "配置指针为空");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->module_count > MAX_MODULES) {
        ESP_LOGE(TAG, "模块数量 %d 超过上限 %d", config->module_count, MAX_MODULES);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->target_count > MAX_TARGETS) {
        ESP_LOGE(TAG, "目标数量 %d 超过上限 %d", config->target_count, MAX_TARGETS);
        return ESP_ERR_INVALID_ARG;
    }

    /* 验证每个模块的超时时间 (1-120 秒) */
    for (int i = 0; i < config->module_count; i++) {
        uint32_t timeout_s = config->modules[i].config.timeout_ms / 1000;
        if (timeout_s < 1 || timeout_s > 120) {
            ESP_LOGE(TAG, "模块 '%s' 超时 %ds 超出范围 [1, 120]",
                     config->modules[i].name, (int)timeout_s);
            return ESP_ERR_INVALID_ARG;
        }
    }

    /* 验证每个目标的端口和模块引用 */
    for (int i = 0; i < config->target_count; i++) {
        const probe_target_t *t = &config->targets[i];

        if (t->port == 0) {
            ESP_LOGE(TAG, "目标 '%s' 端口不能为 0", t->name);
            return ESP_ERR_INVALID_ARG;
        }

        /* 检查引用的模块是否存在 */
        bool module_found = false;
        for (int j = 0; j < config->module_count; j++) {
            if (strcmp(t->module_name, config->modules[j].name) == 0) {
                module_found = true;
                break;
            }
        }
        if (!module_found) {
            ESP_LOGE(TAG, "目标 '%s' 引用的模块 '%s' 不存在", t->name, t->module_name);
            return ESP_ERR_INVALID_ARG;
        }
    }

    /* 抓取间隔 >= 5000ms */
    if (config->scrape_interval_ms < 5000) {
        ESP_LOGE(TAG, "抓取间隔 %dms 低于最小值 5000ms", (int)config->scrape_interval_ms);
        return ESP_ERR_INVALID_ARG;
    }

    /* 指标端口范围 (uint16_t 上限 65535 无需检查) */
    if (config->metrics_port == 0) {
        ESP_LOGE(TAG, "指标端口不能为 0");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* ---- 配置热加载 ---- */

esp_err_t config_reload(void)
{
    ESP_LOGI(TAG, "热加载配置...");

    esp_err_t ret = config_load_from_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "热加载失败 (%s)，保留当前配置", esp_err_to_name(ret));
        return ret;
    }

    ret = config_validate(&s_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "热加载配置验证失败，保留当前配置");
        return ret;
    }

    s_config_version++;
    ESP_LOGI(TAG, "配置已热加载: %d 模块, %d 目标 (version=%d)",
             s_module_count, s_target_count, s_config_version);
    return ESP_OK;
}

/* ---- 公共接口 ---- */

esp_err_t config_manager_init(void)
{
    memset(&s_config, 0, sizeof(s_config));

    /* 1. 挂载 SPIFFS */
    esp_err_t ret = mount_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS 挂载失败，使用工厂默认配置");
        config_get_factory_defaults();
        s_config_version++;
        return ESP_OK;
    }

    /* 2. 尝试从 SPIFFS 加载配置 */
    ret = config_load_from_spiffs();
    if (ret == ESP_OK) {
        /* 3. 验证已加载的配置 */
        ret = config_validate(&s_config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "配置已加载: %d 模块, %d 目标",
                     s_module_count, s_target_count);
            s_config_version++;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "配置验证失败，使用工厂默认配置");
    }

    /* 4. 加载失败或验证失败 - 使用工厂默认配置 */
    config_get_factory_defaults();
    esp_err_t save_ret = config_save_to_spiffs();
    if (save_ret != ESP_OK) {
        ESP_LOGW(TAG, "保存工厂默认配置到 SPIFFS 失败");
    }

    ESP_LOGI(TAG, "使用工厂默认配置: %d 模块, %d 目标",
             s_module_count, s_target_count);
    s_config_version++;
    return ESP_OK;
}

const blackbox_config_t* config_manager_get_config(void)
{
    return &s_config;
}

const blackbox_config_t* config_get_config(void)
{
    return &s_config;
}

void config_get_modules(const probe_module_t **modules, uint8_t *count)
{
    if (modules) *modules = s_modules;
    if (count) *count = s_module_count;
}

void config_get_targets(const probe_target_t **targets, uint8_t *count)
{
    if (targets) *targets = s_targets;
    if (count) *count = s_target_count;
}

const probe_module_t* config_get_module_by_name(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_module_count; i++) {
        if (strcmp(s_modules[i].name, name) == 0) {
            return &s_modules[i];
        }
    }
    return NULL;
}

esp_err_t config_save_config(const blackbox_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = config_validate(config);
    if (ret != ESP_OK) return ret;

    /* 复制模块数据 */
    s_module_count = (config->module_count > MAX_MODULES) ? MAX_MODULES : config->module_count;
    for (int i = 0; i < s_module_count; i++) {
        s_modules[i] = config->modules[i];
    }

    /* 复制目标数据 */
    s_target_count = (config->target_count > MAX_TARGETS) ? MAX_TARGETS : config->target_count;
    for (int i = 0; i < s_target_count; i++) {
        s_targets[i] = config->targets[i];
    }

    s_config.scrape_interval_ms = config->scrape_interval_ms;
    s_config.metrics_port = config->metrics_port;
    update_config_ptr();

    ret = config_save_to_spiffs();
    if (ret == ESP_OK) {
        s_config_version++;
        ESP_LOGI(TAG, "配置已保存: %d 模块, %d 目标", s_module_count, s_target_count);
    }
    return ret;
}

esp_err_t config_update_targets(const char *json_str)
{
    if (!json_str) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = config_parse_json(json_str);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "更新目标: JSON 解析失败");
        return ret;
    }

    ret = config_validate(&s_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "更新目标: 配置验证失败");
        return ret;
    }

    ret = config_save_to_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "更新目标: 保存失败");
        return ret;
    }

    s_config_version++;
    ESP_LOGI(TAG, "目标已更新: %d 模块, %d 目标", s_module_count, s_target_count);
    return ESP_OK;
}

uint8_t config_get_version(void)
{
    return s_config_version;
}

/* ---- NVS WiFi 凭证操作 (保持不变) ---- */

bool config_manager_wifi_has_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace '%s' not found", NVS_NAMESPACE);
        return false;
    }
    size_t needed = 0;
    bool ok = (nvs_get_blob(nvs, NVS_KEY_SSID, NULL, &needed) == ESP_OK && needed > 0);
    if (ok) {
        char ssid[64] = {0};
        size_t ssid_len = sizeof(ssid);
        esp_err_t err = nvs_get_blob(nvs, NVS_KEY_SSID, ssid, &ssid_len);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NVS has WiFi SSID: '%s' (len=%d)", ssid, (int)ssid_len);
        }
    } else {
        ESP_LOGI(TAG, "NVS has no WiFi SSID key");
    }
    nvs_close(nvs);
    return ok;
}

esp_err_t config_manager_wifi_save(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs, NVS_KEY_SSID, ssid, strlen(ssid) + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS save SSID failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_set_blob(nvs, NVS_KEY_PASS, password, strlen(password) + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS save PASS failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved to NVS");
    }
    return err;
}

esp_err_t config_manager_wifi_load(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = ssid_len;
    err = nvs_get_blob(nvs, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }

    len = pass_len;
    err = nvs_get_blob(nvs, NVS_KEY_PASS, password, &len);
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_wifi_clear(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(nvs, NVS_KEY_SSID);
    nvs_erase_key(nvs, NVS_KEY_PASS);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "WiFi credentials cleared from NVS");
    return err;
}

void config_manager_nvs_dump(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "[NVS-DUMP] namespace '%s' not found (err=%s)",
                 NVS_NAMESPACE, esp_err_to_name(err));
        return;
    }

    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find(NVS_DEFAULT_PART_NAME, NVS_NAMESPACE, NVS_TYPE_BLOB, &it);
    int count = 0;
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        ESP_LOGI(TAG, "[NVS-DUMP] key='%s' type=blob", info.key);
        count++;
        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    ESP_LOGI(TAG, "[NVS-DUMP] total keys in namespace '%s': %d", NVS_NAMESPACE, count);

    char ssid[64] = {0};
    char pass[64] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);
    bool has_ssid = (nvs_get_blob(nvs, NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK);
    bool has_pass = (nvs_get_blob(nvs, NVS_KEY_PASS, pass, &pass_len) == ESP_OK);
    ESP_LOGI(TAG, "[NVS-DUMP] wifi_ssid: %s (len=%d)",
             has_ssid ? ssid : "<none>", has_ssid ? (int)ssid_len : 0);
    ESP_LOGI(TAG, "[NVS-DUMP] wifi_pass: %s (len=%d)",
             has_pass ? "***" : "<none>", has_pass ? (int)pass_len : 0);

    nvs_close(nvs);
}
