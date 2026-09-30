/*
 * probe_manager.h - 探测任务调度器接口
 *
 * 管理网络探测任务的生命周期，包括初始化、启动、结果查询和手动触发。
 * 探测任务在独立 FreeRTOS 任务中运行，每个目标独立调度。
 * 支持配置热加载检测，无需重启即可更新探测目标。
 */

#ifndef PROBE_MANAGER_H
#define PROBE_MANAGER_H

#include "esp_err.h"
#include "probe_types.h"

/**
 * @brief 初始化探测管理器
 *
 * 清零探测结果数组和调度时间。
 *
 * @return ESP_OK 初始化成功
 */
esp_err_t probe_manager_init(void);

/**
 * @brief 启动探测任务
 *
 * 创建独立的 FreeRTOS 任务（16KB 栈，优先级 5），
 * 循环执行所有配置的探测目标，每目标独立调度。
 *
 * @return ESP_OK 启动成功，ESP_FAIL 任务创建失败
 */
esp_err_t probe_manager_start(void);

/**
 * @brief 获取探测结果数组
 *
 * @param count 输出参数，返回探测目标数量
 * @return 探测结果数组指针（内部静态存储，不要释放）
 */
const probe_result_t* probe_manager_get_results(uint8_t *count);

/**
 * @brief 获取探测目标数组
 *
 * @param count 输出参数，返回探测目标数量
 * @return 探测目标数组指针（内部静态存储，不要释放）
 */
const probe_target_t* probe_manager_get_targets(uint8_t *count);

/**
 * @brief 触发单次探测 (供 /probe 端点调用)
 *
 * 按目标名称和模块名称立即执行一次探测。
 * 如果目标或模块不存在，或目标正在被探测，返回错误结果。
 * 此函数直接在调用者上下文中执行，不创建新任务。
 *
 * @param target_name 目标名称
 * @param module_name 模块名称
 * @return 探测结果
 */
probe_result_t probe_manager_trigger_probe(const char *target_name, const char *module_name);

/**
 * @brief 对任意主机执行探测 (供 /probe 端点临时目标使用)
 *
 * 不需要预配置目标，直接用主机名/IP、端口和模块名执行探测。
 * 兼容 Prometheus blackbox_exporter 的 /probe?target=X 用法。
 *
 * @param host 主机名或 IP 地址
 * @param port 目标端口 (0 则使用模块默认端口)
 * @param module_name 模块名称
 * @return 探测结果
 */
probe_result_t probe_manager_probe_host(const char *host, uint16_t port,
                                              const char *module_name);

#endif
