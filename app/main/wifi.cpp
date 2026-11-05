#include "wifi.hpp"

#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi_credentials.h"

static_assert(sizeof(WIFI_SSID) - 1 >= 1 && sizeof(WIFI_SSID) - 1 <= 32, "WIFI_SSID must be 1-32 bytes");
static_assert(sizeof(WIFI_PASSWORD) - 1 == 0 || (sizeof(WIFI_PASSWORD) - 1 >= 8 && sizeof(WIFI_PASSWORD) - 1 <= 64),
              "WIFI_PASSWORD must be empty (open network) or 8-64 bytes");

namespace wifi {
namespace {

constexpr const char *TAG = "wifi";

StatusCallback s_cb;
void *s_cb_ctx;

const char *reason_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:             return "AP not found";
    case WIFI_REASON_AUTH_FAIL:               return "auth failed";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:       return "wrong password?";
    case WIFI_REASON_BEACON_TIMEOUT:          return "signal lost";
    default:                                  return nullptr;
    }
}

void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "connecting to \"%s\"", WIFI_SSID);
        s_cb(Status::Connecting, WIFI_SSID, s_cb_ctx);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *ev = static_cast<const wifi_event_sta_disconnected_t *>(data);
        const char *name = reason_name(ev->reason);
        char detail[32];
        if (name != nullptr) {
            snprintf(detail, sizeof(detail), "%s", name);
        } else {
            snprintf(detail, sizeof(detail), "reason %u", ev->reason);
        }
        ESP_LOGW(TAG, "disconnected: %s (reason %u), retrying", detail, ev->reason);
        s_cb(Status::Disconnected, detail, s_cb_ctx);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *ev = static_cast<const ip_event_got_ip_t *>(data);
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "connected, IP %s", ip);
        s_cb(Status::Connected, ip, s_cb_ctx);
    }
}

}

esp_err_t start(StatusCallback cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(cb != nullptr && s_cb == nullptr, ESP_ERR_INVALID_STATE, TAG, "invalid call");
    s_cb = cb;
    s_cb_ctx = ctx;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS init");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != nullptr, ESP_FAIL, TAG, "STA netif");

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr, nullptr),
                        TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr, nullptr),
                        TAG, "IP events");

    wifi_config_t cfg = {};
    memcpy(cfg.sta.ssid, WIFI_SSID, sizeof(WIFI_SSID) - 1);
    memcpy(cfg.sta.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD) - 1);
    cfg.sta.threshold.authmode = sizeof(WIFI_PASSWORD) > 1 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "config");
    return esp_wifi_start();
}

}
