#include <cstdlib>
#include <ctime>
#include <sys/time.h>

#include "display.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "imu.hpp"
#include "time_sync.hpp"
#include "wifi.hpp"

namespace {

constexpr const char *TIME_ZONE = "JST-9";
constexpr const char *TAG = "app";

struct Ui {
    lv_display_t *disp = nullptr;
    lv_obj_t *wifi = nullptr;
    lv_obj_t *time = nullptr;
    lv_obj_t *date = nullptr;
    time_t shown_sec = 0;
};

Ui s_ui;

void ui_create(Ui &ui)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x102040), 0);

    ui.wifi = lv_label_create(scr);
    lv_label_set_text(ui.wifi, "Wi-Fi: starting");
    lv_obj_set_width(ui.wifi, 200);
    lv_label_set_long_mode(ui.wifi, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(ui.wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ui.wifi, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_text_align(ui.wifi, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.wifi, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *clock = lv_obj_create(scr);
    lv_obj_remove_style_all(clock);
    lv_obj_set_size(clock, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(clock, 6, 0);
    lv_obj_align(clock, LV_ALIGN_CENTER, 0, -10);

    ui.time = lv_label_create(clock);
    lv_obj_set_style_text_font(ui.time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ui.time, lv_color_white(), 0);
    lv_label_set_text(ui.time, "--:--:--");

    ui.date = lv_label_create(clock);
    lv_obj_set_style_text_font(ui.date, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ui.date, lv_color_hex(0x80C0FF), 0);
    lv_label_set_text(ui.date, "syncing...");
    ui.shown_sec = 0;
}

void clock_tick(lv_timer_t *timer)
{
    auto &ui = *static_cast<Ui *>(lv_timer_get_user_data(timer));
    if (!time_sync::is_synced()) {
        if (ui.shown_sec != 0) {
            lv_label_set_text(ui.time, "--:--:--");
            lv_label_set_text(ui.date, "syncing...");
            ui.shown_sec = 0;
        }
        return;
    }
    timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec == ui.shown_sec) {
        return;
    }
    ui.shown_sec = tv.tv_sec;
    tm local;
    localtime_r(&tv.tv_sec, &local);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", &local);
    lv_label_set_text(ui.time, buf);
    strftime(buf, sizeof(buf), "%Y-%m-%d %a", &local);
    lv_label_set_text(ui.date, buf);
}

void on_wifi_status(wifi::Status status, const char *detail, void *ctx)
{
    auto &ui = *static_cast<Ui *>(ctx);
    if (lvgl_port_lock(0)) {
        switch (status) {
        case wifi::Status::Connecting:
            lv_label_set_text_fmt(ui.wifi, "Wi-Fi: connecting to %s", detail);
            lv_obj_set_style_text_color(ui.wifi, lv_color_hex(0xFFD060), 0);
            break;
        case wifi::Status::Connected:
            lv_label_set_text_fmt(ui.wifi, "Wi-Fi: %s", detail);
            lv_obj_set_style_text_color(ui.wifi, lv_color_hex(0x60FF90), 0);
            break;
        case wifi::Status::Disconnected:
            lv_label_set_text_fmt(ui.wifi, "Wi-Fi: retrying (%s)", detail);
            lv_obj_set_style_text_color(ui.wifi, lv_color_hex(0xFF7070), 0);
            break;
        }
        lvgl_port_unlock();
    }
}

void on_orientation(imu::Orientation orientation, void *ctx)
{
    auto &ui = *static_cast<Ui *>(ctx);
    ESP_LOGI(TAG, "orientation: %s", imu::name(orientation));
    lv_display_rotation_t rotation;
    switch (orientation) {
    case imu::Orientation::Up:    rotation = LV_DISPLAY_ROTATION_0;   break;
    case imu::Orientation::Right: rotation = LV_DISPLAY_ROTATION_90;  break;
    case imu::Orientation::Left:  rotation = LV_DISPLAY_ROTATION_270; break;
    default:                      return;
    }
    if (lvgl_port_lock(0)) {
        lv_display_set_rotation(ui.disp, rotation);
        lvgl_port_unlock();
    }
}

}

extern "C" void app_main()
{
    setenv("TZ", TIME_ZONE, 1);
    tzset();

    lv_display_t *disp = display::init();
    if (disp == nullptr) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    s_ui.disp = disp;
    if (lvgl_port_lock(0)) {
        ui_create(s_ui);
        lv_timer_create(clock_tick, 100, &s_ui);
        lv_refr_now(disp);
        lvgl_port_unlock();
    }
    display::backlight_on();

    ESP_ERROR_CHECK(imu::start(on_orientation, &s_ui));
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(time_sync::start());
    ESP_ERROR_CHECK(wifi::start(on_wifi_status, &s_ui));
    ESP_LOGI(TAG, "ready");
}
