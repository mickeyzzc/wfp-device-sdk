#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* 探测模块类型枚举 */
typedef enum {
    MODULE_HTTP,       /* HTTP 模块 */
    MODULE_HTTPS,     /* HTTPS 模块 */
    MODULE_TCP,      /* TCP 模块 */
    MODULE_TCP_TLS,   /* TCP+TLS 模块 */
    MODULE_DNS,       /* DNS 模块 */
    MODULE_ICMP,      /* ICMP 模块 */
    MODULE_WS,        /* WebSocket 模块 */
    MODULE_WSS,       /* WSS 模块 */
} probe_module_type_t;

/* 常量定义 */
#define MAX_MODULES    16      /* 最大模块数量 */
#define MAX_TARGETS   32      /* 最大目标数量 */
#define MAX_HEADERS  4       /* 最大请求头数量 */

/* HTTP 模块配置结构体 */
typedef struct {
    char method[8];                          /* HTTP 方法: GET, HEAD, POST 等 */
    uint16_t valid_status_codes[8];            /* 有效的 HTTP 状态码 */
    uint8_t valid_status_count;               /* 有效状态码数量 */
    bool no_follow_redirects;                  /* 不跟随重定向 */
    char headers[MAX_HEADERS][64];              /* 自定义请求头 */
    uint8_t header_count;                      /* 请求头数量 */
} http_module_config_t;

/* TCP 模块配置结构体 */
typedef struct {
    bool tls;                                 /* 是否使用 TLS */
    char query[64];                          /* 查询字符串 */
    char expected_response[64];               /* 期望的响应字符串 */
} tcp_module_config_t;

/* DNS 模块配置结构体 */
typedef struct {
    char query_name[128];                      /* 查询的域名 */
    uint8_t query_type;                       /* 查询类型: 1=A, 28=AAAA 等 */
} dns_module_config_t;

/* ICMP 模块配置结构体 */
typedef struct {
    uint8_t packets;                         /* 发送的 ICMP 数据包数量 */
    uint16_t payload_size;                   /* 数据负载大小 (字节) */
    uint8_t pattern;                      /* 负载填充模式 */
} icmp_module_config_t;

/* 模块配置联合体 */
typedef union {
    http_module_config_t http;                 /* HTTP 模块配置 */
    tcp_module_config_t tcp;                 /* TCP 模块配置 */
    dns_module_config_t dns;                 /* DNS 模块配置 */
    icmp_module_config_t icmp;               /* ICMP 模块配置 */
} module_config_union_t;

/* 探测模块配置结构体 */
typedef struct {
    probe_module_type_t type;                /* 模块类型 */
    uint32_t timeout_ms;                    /* 超时时间 (毫秒) */
    module_config_union_t config;            /* 模块配置联合体 */
} probe_module_config_t;

/* 探测模块定义结构体 */
typedef struct {
    char name[32];                        /* 模块名称 */
    probe_module_config_t config;          /* 模块配置 */
} probe_module_t;

/* 探测目标结构体 (重构后) */
typedef struct {
    char name[64];                        /* 目标名称 */
    char target[256];                    /* 目标主机名或 IP */
    uint16_t port;                      /* 目标端口 */
    uint32_t interval_ms;                 /* 探测间隔 (毫秒) */
    char module_name[32];                 /* 使用的模块名称 */
} probe_target_t;

/* 探测类型枚举 (兼容旧版) */
typedef enum {
    PROBE_TYPE_HTTP,                      /* HTTP 探测 */
    PROBE_TYPE_HTTPS,                   /* HTTPS 探测 */
    PROBE_TYPE_TCP,                     /* TCP 探测 */
    PROBE_TYPE_TCP_TLS,                 /* TCP+TLS 探测 */
    PROBE_TYPE_DNS,                     /* DNS 探测 */
    PROBE_TYPE_ICMP,                   /* ICMP 探测 */
    PROBE_TYPE_WS,                      /* WebSocket 探测 */
    PROBE_TYPE_WSS,                     /* WSS 探测 */
} probe_type_t;

/* 黑盒配置结构体 (扩展后) */
typedef struct {
    probe_module_t *modules;              /* 探测模块数组 */
    uint8_t module_count;               /* 模块数量 */
    probe_target_t *targets;             /* 探测目标数组 */
    uint8_t target_count;               /* 探测目标数量 */
    uint32_t scrape_interval_ms;         /* 抓取间隔 (毫秒) */
    uint16_t metrics_port;              /* 指标服务器端口 */
} blackbox_config_t;

/* 初始化配置管理器 */
esp_err_t config_manager_init(void);

/* 获取配置信息 */
const blackbox_config_t* config_manager_get_config(void);

/* NVS WiFi 凭证存储 */
#define NVS_NAMESPACE  "blackbox"   /* NVS 命名空间 */
#define NVS_KEY_SSID   "wifi_ssid"  /* WiFi SSID 键 */
#define NVS_KEY_PASS   "wifi_pass"  /* WiFi 密码键 */

/* 检查是否已存储 WiFi 凭证 */
bool config_manager_wifi_has_credentials(void);

/* 保存 WiFi 凭证到 NVS */
esp_err_t config_manager_wifi_save(const char *ssid, const char *password);

/* 从 NVS 加载 WiFi 凭证 */
esp_err_t config_manager_wifi_load(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len);

/* 清除 NVS 中的 WiFi 凭证 */
esp_err_t config_manager_wifi_clear(void);

/* 调试：打印 NVS 中存储的所有 WiFi 凭证内容 */
void config_manager_nvs_dump(void);

/* ---- 新增配置访问接口 ---- */

/* 获取配置版本号 (用于热加载检测) */
uint8_t config_get_version(void);

/* 获取所有探测模块 */
void config_get_modules(const probe_module_t **modules, uint8_t *count);

/* 获取所有探测目标 */
void config_get_targets(const probe_target_t **targets, uint8_t *count);

/* 按名称查找探测模块 (O(n) 搜索，最多 MAX_MODULES 次迭代) */
const probe_module_t* config_get_module_by_name(const char *name);

/* 保存完整配置到 SPIFFS */
esp_err_t config_save_config(const blackbox_config_t *config);

/* 通过 JSON 字符串更新配置 (Web UI 提交) */
esp_err_t config_update_targets(const char *json_str);

/* 获取配置指针 (兼容接口) */
const blackbox_config_t* config_get_config(void);

/* 热加载配置 (重新从 SPIFFS 读取) */
esp_err_t config_reload(void);

#endif