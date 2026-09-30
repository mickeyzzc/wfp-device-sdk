#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * LED 状态指示枚举
 * @brief 表示 LED 当前显示的状态
 */
typedef enum {
    STATUS_LED_INIT,            /* 初始化中 */
    STATUS_LED_AP_MODE,         /* AP 模式 */
    STATUS_LED_STA_CONNECTING,  /* STA 连接中 */
    STATUS_LED_CONNECTED,       /* 已连接 (正常状态) */
    STATUS_LED_DISCONNECTED,    /* 已断开连接 */
    STATUS_LED_CONNECTION_FAILED,/* 连接失败 */
    STATUS_LED_SELF_TEST,       /* 自检中 */
    STATUS_LED_CONFIG_RELOAD    /* 配置重载 */
} status_led_state_t;

#ifdef CONFIG_ESP_STATUS_LED

/* 板卡特定的 GPIO 定义 */
#if CONFIG_IDF_TARGET_ESP32C3
    #define STATUS_LED_GPIO_NUM 8
#elif CONFIG_IDF_TARGET_ESP32C6
    #define STATUS_LED_GPIO_NUM 15
#else
    #warning "未识别的目标芯片，LED 状态未定义"
#endif

/**
 * 初始化 LED 指示器
 * @return ESP_OK 成功, ESP_FAIL 失败
 */
esp_err_t status_led_init(void);

/**
 * 设置 LED 状态
 * @param state LED 状态
 */
void status_led_set_state(status_led_state_t state);

#else /* CONFIG_ESP_STATUS_LED 未启用时，提供空操作存根 */

static inline esp_err_t status_led_init(void) {
    return ESP_OK;
}

static inline void status_led_set_state(status_led_state_t state) {
    (void)state;
}

#endif /* CONFIG_ESP_STATUS_LED */

#endif /* STATUS_LED_H */
