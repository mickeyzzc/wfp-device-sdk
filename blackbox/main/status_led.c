/*
 * status_led.c - LED 状态指示模块
 *
 * 通过板载 LED 显示设备当前状态。
 * ESP32-C3: GPIO8 单色 LED（低电平点亮）。
 * ESP32-C6: GPIO15 单色 LED（高电平点亮）。
 *
 * 版权所有 (C) 2026 ESP32 Blackbox 项目
 *
 * 本程序为自由软件，遵循 MIT 许可证分发。
 */

#include "sdkconfig.h"

#ifdef CONFIG_ESP_STATUS_LED

#include "status_led.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG = "STATUS_LED";

/* 静态全局变量 */
static status_led_state_t s_current_state = STATUS_LED_INIT;
static bool s_initialized = false;
static TaskHandle_t s_led_task_handle = NULL;
static portMUX_TYPE s_state_mutex = portMUX_INITIALIZER_UNLOCKED;

/* LED 任务栈大小和优先级 */
#define LED_TASK_STACK_SIZE  3072
#define LED_TASK_PRIORITY    (tskIDLE_PRIORITY + 1)

/* 闪烁参数（毫秒） */
#define BLINK_FAST_MS        50    /* 快闪：50ms 亮 / 50ms 灭 */
#define BLINK_MEDIUM_MS      100   /* 中速闪：100ms 亮 / 100ms 灭 */
#define BLINK_SLOW_MS        250   /* 慢闪：250ms 亮 / 250ms 灭 */
#define PAUSE_LONG_MS        1000  /* 长暂停 */

/* LED GPIO 控制 - C3 active-low, C6 active-high */
#if CONFIG_IDF_TARGET_ESP32C3
static void led_on(void) { gpio_set_level(STATUS_LED_GPIO_NUM, 0); }
static void led_off(void) { gpio_set_level(STATUS_LED_GPIO_NUM, 1); }
#elif CONFIG_IDF_TARGET_ESP32C6
static void led_on(void) { gpio_set_level(STATUS_LED_GPIO_NUM, 1); }
static void led_off(void) { gpio_set_level(STATUS_LED_GPIO_NUM, 0); }
#endif

/**
 * @brief LED 闪烁
 * @param on_ms  亮灯持续时间（毫秒）
 * @param off_ms 灭灯持续时间（毫秒）
 * @param count  闪烁次数
 */
static void led_blink(uint32_t on_ms, uint32_t off_ms, int count)
{
    for (int i = 0; i < count; i++) {
        led_on();
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

/* ── 事件处理函数 ── */

/**
 * @brief WiFi 事件处理
 *
 * 监听 WiFi 状态变化并切换 LED 模式。
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        status_led_set_state(STATUS_LED_STA_CONNECTING);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        status_led_set_state(STATUS_LED_DISCONNECTED);
    } else if (event_id == WIFI_EVENT_AP_START) {
        status_led_set_state(STATUS_LED_AP_MODE);
    }
}

/**
 * @brief IP 事件处理
 *
 * 获取到 IP 地址时切换到已连接状态。
 */
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        status_led_set_state(STATUS_LED_CONNECTED);
    }
}

/* ── LED 任务 ── */

/**
 * @brief LED 状态指示任务
 *
 * 根据当前状态驱动板载 LED 显示对应的闪烁模式。
 * 统一使用 GPIO 控制 LED，C3 低电平点亮，C6 高电平点亮。
 */
static void led_task(void *arg)
{
    ESP_LOGI(TAG, "LED 任务启动");

    while (1) {
        /* 读取当前状态（临界区保护） */
        status_led_state_t state;
        portENTER_CRITICAL(&s_state_mutex);
        state = s_current_state;
        portEXIT_CRITICAL(&s_state_mutex);

        switch (state) {
        case STATUS_LED_INIT:
            /* 初始化中：快闪 100ms（50ms 亮 + 50ms 灭） */
            led_blink(BLINK_FAST_MS, BLINK_FAST_MS, 1);
            break;

        case STATUS_LED_AP_MODE:
            /* AP 模式：慢闪 500ms（250ms 亮 + 250ms 灭） */
            led_blink(BLINK_SLOW_MS, BLINK_SLOW_MS, 1);
            break;

        case STATUS_LED_STA_CONNECTING:
            /* STA 连接中：中速闪 200ms（100ms 亮 + 100ms 灭） */
            led_blink(BLINK_MEDIUM_MS, BLINK_MEDIUM_MS, 1);
            break;

        case STATUS_LED_CONNECTED:
            /* 已连接：常亮 */
            led_on();
            /* 避免忙循环，长时间挂起 */
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case STATUS_LED_DISCONNECTED:
            /* 已断开：快闪 100ms（50ms 亮 + 50ms 灭） */
            led_blink(BLINK_FAST_MS, BLINK_FAST_MS, 1);
            break;

        case STATUS_LED_CONNECTION_FAILED:
            /* 连接失败：快闪 3 次后暂停 1 秒 */
            led_blink(BLINK_FAST_MS, BLINK_FAST_MS, 3);
            led_off();
            vTaskDelay(pdMS_TO_TICKS(PAUSE_LONG_MS));
            break;

        case STATUS_LED_SELF_TEST:
            /* 自检中：快闪 100ms */
            led_blink(BLINK_FAST_MS, BLINK_FAST_MS, 1);
            break;

        case STATUS_LED_CONFIG_RELOAD:
            /* 配置重载：闪烁 2 次后等待外部恢复状态 */
            led_blink(BLINK_MEDIUM_MS, BLINK_MEDIUM_MS, 2);
            /* 闪烁完成后短暂等待，由调用方恢复之前的状态 */
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }

        /* 短暂让出 CPU，防止状态未变时占用过多时间片 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ── 公共接口 ── */

esp_err_t status_led_init(void)
{
    /* 防止重复初始化 */
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;

    /* 配置 GPIO 为输出模式 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO_NUM),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* C3: 初始高电平（LED 灭，低电平点亮）；C6: 初始低电平（LED 灭） */
#if CONFIG_IDF_TARGET_ESP32C3
    gpio_set_level(STATUS_LED_GPIO_NUM, 1);
#elif CONFIG_IDF_TARGET_ESP32C6
    gpio_set_level(STATUS_LED_GPIO_NUM, 0);
#endif

    /* 创建 LED 状态指示任务 */
    BaseType_t task_ret = xTaskCreate(led_task, "status_led",
                                      LED_TASK_STACK_SIZE, NULL,
                                      LED_TASK_PRIORITY, &s_led_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "LED 任务创建失败");
        return ESP_FAIL;
    }

    /* 注册 WiFi 事件处理 */
    esp_event_handler_instance_t wifi_inst = NULL;
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL, &wifi_inst);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 事件注册失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 注册 IP 事件处理 */
    esp_event_handler_instance_t ip_inst = NULL;
    ret = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                               ip_event_handler, NULL, &ip_inst);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP 事件注册失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "LED 状态指示器初始化完成");

    return ESP_OK;
}

void status_led_set_state(status_led_state_t state)
{
    if (!s_initialized) {
        return;
    }
    portENTER_CRITICAL(&s_state_mutex);
    s_current_state = state;
    portEXIT_CRITICAL(&s_state_mutex);
    ESP_LOGI(TAG, "LED 状态切换: %d", (int)state);
}

#endif /* CONFIG_ESP_STATUS_LED */
