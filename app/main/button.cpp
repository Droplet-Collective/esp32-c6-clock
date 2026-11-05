#include "button.hpp"

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace button {
namespace {

constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_18;
constexpr TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(30);
constexpr TickType_t RELEASE_POLL_TICKS = pdMS_TO_TICKS(10);

constexpr const char *TAG = "btton";

TaskHandle_t s_task;
PressCallback s_cb;
void *s_cb_ctx;

bool pressed()
{
    return gpio_get_level(BUTTON_PIN) == 0;
}

void button_isr(void *)
{
    BaseType_t woken = pdFALSE;
    gpio_intr_disable(BUTTON_PIN);
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

void button_task(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(DEBOUNCE_TICKS);
        if (pressed()) {
            s_cb(s_cb_ctx);
            while (pressed()) {
                vTaskDelay(RELEASE_POLL_TICKS);
            }
            vTaskDelay(DEBOUNCE_TICKS);
        }
        gpio_intr_enable(BUTTON_PIN);
    }
}

}

esp_err_t start(PressCallback cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(cb != nullptr && s_task == nullptr, ESP_ERR_INVALID_STATE, TAG, "invalid call");
    s_cb = cb;
    s_cb_ctx = ctx;

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << BUTTON_PIN;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.intr_type = GPIO_INTR_NEGEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "GPIO");

    ESP_RETURN_ON_FALSE(xTaskCreate(button_task, "button", 3072, nullptr, 4, &s_task) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return gpio_isr_handler_add(BUTTON_PIN, button_isr, nullptr);
}

}
