#include "time_sync.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

namespace time_sync {
namespace {

constexpr const char *TIME_URL = "https://time.poropo.ro/v1/unix_ms?format=text";
constexpr const char *TIME_USER_AGENT = "esp32-c6-stack (ESP-IDF " IDF_VER ")";
constexpr uint32_t SYNC_INTERVAL_MS = 60 * 60 * 1000;
constexpr uint32_t RETRY_INTERVAL_MS = 60 * 1000;
constexpr int HTTP_TIMEOUT_MS = 10000;
constexpr EventBits_t ONLINE_BIT = BIT0;

constexpr const char *TAG = "time_sync";

EventGroupHandle_t s_events;
std::atomic<bool> s_synced{false};

struct Fetch {
    int64_t connected_us = 0;
    int64_t response_us = 0;
    char body[24] = {};
    size_t body_len = 0;
};

esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    auto *f = static_cast<Fetch *>(evt->user_data);
    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        f->connected_us = esp_timer_get_time();
        break;
    case HTTP_EVENT_ON_STATUS_CODE:
        f->response_us = esp_timer_get_time();
        break;
    case HTTP_EVENT_ON_DATA: {
        const size_t room = sizeof(f->body) - 1 - f->body_len;
        const size_t n = static_cast<size_t>(evt->data_len) < room ? static_cast<size_t>(evt->data_len) : room;
        memcpy(f->body + f->body_len, evt->data, n);
        f->body_len += n;
        f->body[f->body_len] = '\0';
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t sync_once()
{
    Fetch f;
    esp_http_client_config_t cfg = {};
    cfg.url = TIME_URL;
    cfg.user_agent = TIME_USER_AGENT;
    cfg.timeout_ms = HTTP_TIMEOUT_MS;
    cfg.event_handler = on_http_event;
    cfg.user_data = &f;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client != nullptr, ESP_ERR_NO_MEM, TAG, "client init");
    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    ESP_RETURN_ON_ERROR(err, TAG, "request failed");
    ESP_RETURN_ON_FALSE(status == 200, ESP_FAIL, TAG, "HTTP status %d", status);
    ESP_RETURN_ON_FALSE(f.connected_us != 0 && f.response_us >= f.connected_us, ESP_FAIL, TAG, "missing timing");

    char *end;
    const long long server_ms = strtoll(f.body, &end, 10);
    ESP_RETURN_ON_FALSE(end != f.body && server_ms > 1700000000000LL, ESP_FAIL, TAG, "bad body \"%s\"", f.body);

    const int64_t rtt_us = f.response_us - f.connected_us;
    const int64_t now_us = server_ms * 1000 + rtt_us / 2 + (esp_timer_get_time() - f.response_us);

    timeval before;
    gettimeofday(&before, nullptr);
    const timeval tv = {.tv_sec = static_cast<time_t>(now_us / 1000000), .tv_usec = static_cast<suseconds_t>(now_us % 1000000)};
    ESP_RETURN_ON_FALSE(settimeofday(&tv, nullptr) == 0, ESP_FAIL, TAG, "settimeofday");

    const int64_t before_us = static_cast<int64_t>(before.tv_sec) * 1000000 + before.tv_usec;
    if (s_synced.load()) {
        ESP_LOGI(TAG, "synced, rtt %lld ms, corrected %+lld ms", rtt_us / 1000, (now_us - before_us) / 1000);
    } else {
        ESP_LOGI(TAG, "synced, rtt %lld ms", rtt_us / 1000);
    }
    s_synced.store(true);
    return ESP_OK;
}

void sync_task(void *)
{
    for (;;) {
        xEventGroupWaitBits(s_events, ONLINE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        const bool ok = sync_once() == ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(ok ? SYNC_INTERVAL_MS : RETRY_INTERVAL_MS));
    }
}

void on_net_event(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, ONLINE_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, ONLINE_BIT);
    }
}

}

esp_err_t start()
{
    ESP_RETURN_ON_FALSE(s_events == nullptr, ESP_ERR_INVALID_STATE, TAG, "already started");
    s_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_events != nullptr, ESP_ERR_NO_MEM, TAG, "event group");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_net_event, nullptr, nullptr),
                        TAG, "IP events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_net_event, nullptr, nullptr),
                        TAG, "Wi-Fi events");
    ESP_RETURN_ON_FALSE(xTaskCreate(sync_task, "time_sync", 8192, nullptr, 3, nullptr) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

bool is_synced()
{
    return s_synced.load();
}

}
