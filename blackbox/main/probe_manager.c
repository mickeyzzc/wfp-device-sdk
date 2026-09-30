/*
 * probe_manager.c - 探测任务调度器实现
 *
 * 管理所有网络探测任务的调度和执行。
 * 每个目标独立调度，基于各自间隔运行。
 * 支持配置热加载检测和手动触发探测。
 */

#include "probe_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "config_manager.h"

static const char *TAG = "PROBE_MGR";

static probe_result_t s_results[MAX_TARGETS];
static int64_t s_next_run_time[MAX_TARGETS];  /* 每个目标的下次运行时间 (us since boot) */
static uint8_t s_last_config_version = 0;     /* 热加载检测 */
static TaskHandle_t s_probe_task_handle = NULL;
static bool s_probe_busy[MAX_TARGETS];         /* 防止同一目标并发探测 */

/**
 * @brief 根据模块类型执行探测
 *
 * @param target    探测目标
 * @param module    探测模块配置
 * @return 探测结果
 */
static probe_result_t dispatch_probe(const probe_target_t *target,
                                     const probe_module_t *module)
{
    switch (module->config.type) {
        case MODULE_HTTP:
            return probe_http_execute(target, &module->config);
        case MODULE_HTTPS:
            return probe_https_execute(target, &module->config);
        case MODULE_TCP:
            return probe_tcp_execute(target, &module->config);
        case MODULE_TCP_TLS:
            return probe_tcp_tls_execute(target, &module->config);
        case MODULE_DNS:
            return probe_dns_execute(target, &module->config);
        case MODULE_ICMP:
            return probe_icmp_execute(target, &module->config);
        case MODULE_WS:
            return probe_ws_execute(target, &module->config);
        case MODULE_WSS:
            return probe_wss_execute(target, &module->config);
        default: {
            probe_result_t err = {0};
            snprintf(err.error_msg, sizeof(err.error_msg),
                     "Unknown module type %d", module->config.type);
            return err;
        }
    }
}

/**
 * @brief 探测任务主函数
 *
 * 无限循环执行所有配置的探测目标：
 * 1. 检测配置热加载变化
 * 2. 每个目标独立调度，基于各自的间隔运行
 * 3. 通过模块名称查找模块配置并分发探测
 * 4. 没有目标到期时休眠 100ms 避免忙等待
 */
static void probe_task(void *pvParameters)
{
    /* 挂看门狗：探测为同步网络调用（模块超时可到秒级），全局超时放宽到
     * 30s（sdkconfig CONFIG_ESP_TASK_WDT_TIMEOUT_S=30）；卡死 >30s panic 重启自恢复 */
    esp_task_wdt_add(NULL);

    const probe_target_t *targets = NULL;
    const probe_module_t *modules = NULL;
    uint8_t target_count = 0;
    uint8_t module_count = 0;

    /* 首次加载配置 */
    config_get_targets(&targets, &target_count);
    config_get_modules(&modules, &module_count);
    s_last_config_version = config_get_version();

    while (1) {
        esp_task_wdt_reset();
        /* 热加载检测 */
        uint8_t cur_version = config_get_version();
        if (cur_version != s_last_config_version) {
            config_get_targets(&targets, &target_count);
            config_get_modules(&modules, &module_count);

            /* 新目标立即执行（next_run=0），已有目标保持不变 */
            for (int i = 0; i < target_count && i < MAX_TARGETS; i++) {
                if (s_next_run_time[i] == 0 && s_results[i].success == 0
                    && s_results[i].duration_ms == 0 && s_results[i].error_msg[0] == '\0') {
                    /* 全新目标，保持 next_run=0 让它立即运行 */
                }
            }

            s_last_config_version = cur_version;
            ESP_LOGI(TAG, "配置热加载完成, %d 个目标, %d 个模块",
                     target_count, module_count);
        }

        if (target_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int64_t now = esp_timer_get_time();
        int best_idx = -1;
        int64_t best_time = INT64_MAX;

        /* 查找最早到期且已过期的目标 */
        for (int i = 0; i < target_count && i < MAX_TARGETS; i++) {
            if (s_next_run_time[i] <= now && s_next_run_time[i] < best_time) {
                best_time = s_next_run_time[i];
                best_idx = i;
            }
        }

        if (best_idx >= 0) {
            const probe_target_t *target = &targets[best_idx];

            /* 通过模块名称查找模块配置 */
            const probe_module_t *module = config_get_module_by_name(target->module_name);
            if (module == NULL) {
                ESP_LOGE(TAG, "未找到模块 '%s' (目标: %s)，跳过",
                         target->module_name, target->name);
                s_next_run_time[best_idx] = now + 5000000LL; /* 5s 后重试 */
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            probe_result_t result = dispatch_probe(target, module);
            s_results[best_idx] = result;

            /* 设置下次运行时间: now + interval_ms * 1000 (ms -> us) */
            s_next_run_time[best_idx] = now + (int64_t)target->interval_ms * 1000LL;

            ESP_LOGI(TAG, "探测 %s (%s:%d): success=%d, duration=%dms",
                     target->name, target->target, target->port,
                     result.success, result.duration_ms);

            /* 探测间隔 100ms，避免看门狗超时 */
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            /* 没有目标到期，休眠 100ms */
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t probe_manager_init(void)
{
    memset(s_results, 0, sizeof(s_results));
    memset(s_next_run_time, 0, sizeof(s_next_run_time));
    memset(s_probe_busy, 0, sizeof(s_probe_busy));
    ESP_LOGI(TAG, "探测管理器初始化完成");
    return ESP_OK;
}

esp_err_t probe_manager_start(void)
{
    BaseType_t ret = xTaskCreate(probe_task, "probe_task", 16384, NULL, 5, &s_probe_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建探测任务失败");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "探测管理器已启动");
    return ESP_OK;
}

const probe_result_t* probe_manager_get_results(uint8_t *count)
{
    const probe_target_t *targets = NULL;
    config_get_targets(&targets, count);
    return s_results;
}

const probe_target_t* probe_manager_get_targets(uint8_t *count)
{
    const probe_target_t *targets = NULL;
    config_get_targets(&targets, count);
    return targets;
}

probe_result_t probe_manager_trigger_probe(const char *target_name, const char *module_name)
{
    probe_result_t result = {0};

    /* 查找目标 */
    const probe_target_t *targets = NULL;
    uint8_t target_count = 0;
    config_get_targets(&targets, &target_count);

    int target_idx = -1;
    for (int i = 0; i < target_count && i < MAX_TARGETS; i++) {
        if (strcmp(targets[i].name, target_name) == 0) {
            target_idx = i;
            break;
        }
    }

    if (target_idx < 0) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "目标 '%s' 未找到", target_name);
        return result;
    }

    /* 查找模块 */
    const probe_module_t *module = config_get_module_by_name(module_name);
    if (module == NULL) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "模块 '%s' 未找到", module_name);
        return result;
    }

    /* 检查并发 */
    if (s_probe_busy[target_idx]) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "目标 '%s' 探测正在运行", target_name);
        return result;
    }

    /* 执行探测 (直接调用，不使用 FreeRTOS 任务) */
    s_probe_busy[target_idx] = true;
    result = dispatch_probe(&targets[target_idx], module);
    s_results[target_idx] = result;
    s_probe_busy[target_idx] = false;

    ESP_LOGI(TAG, "手动触发探测 %s (%s:%d): success=%d, duration=%dms",
             targets[target_idx].name, targets[target_idx].target,
             targets[target_idx].port, result.success, result.duration_ms);

    return result;
}

probe_result_t probe_manager_probe_host(const char *host, uint16_t port,
                                              const char *module_name)
{
    probe_result_t result = {0};

    /* 查找模块 */
    const probe_module_t *module = config_get_module_by_name(module_name);
    if (module == NULL) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "模块 '%s' 未找到", module_name);
        return result;
    }

    /* 构造临时目标 */
    probe_target_t tmp = {0};
    strncpy(tmp.target, host, sizeof(tmp.target) - 1);
    tmp.port = port;
    strncpy(tmp.module_name, module_name, sizeof(tmp.module_name) - 1);

    /* 直接执行探测 */
    result = dispatch_probe(&tmp, module);

    ESP_LOGI(TAG, "临时探测 %s:%d (%s): success=%d, duration=%dms",
             host, port, module_name, result.success, result.duration_ms);

    return result;
}
