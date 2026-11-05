#include "display.hpp"

#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lvgl_port.h"

namespace display {
namespace {

constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
constexpr int LCD_PIXEL_CLOCK = 40 * 1000 * 1000;
constexpr int LCD_H_RES = 240;
constexpr int LCD_V_RES = 240;
constexpr gpio_num_t LCD_PIN_SCLK = GPIO_NUM_1;
constexpr gpio_num_t LCD_PIN_MOSI = GPIO_NUM_2;
constexpr gpio_num_t LCD_PIN_DC = GPIO_NUM_3;
constexpr gpio_num_t LCD_PIN_RST = GPIO_NUM_4;
constexpr gpio_num_t LCD_PIN_BL = GPIO_NUM_6;
constexpr gpio_num_t LCD_PIN_CS = GPIO_NUM_11;
constexpr int LCD_DRAW_LINES = 40;
constexpr ledc_mode_t BL_MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t BL_TIMER = LEDC_TIMER_0;
constexpr ledc_channel_t BL_CHANNEL = LEDC_CHANNEL_0;
constexpr ledc_timer_bit_t BL_RESOLUTION = LEDC_TIMER_10_BIT;
constexpr uint32_t BL_FREQ_HZ = 20000;
constexpr uint32_t BL_DUTY_MAX = (1U << BL_RESOLUTION) - 1;

void backlight_init()
{
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = BL_MODE;
    timer_cfg.duty_resolution = BL_RESOLUTION;
    timer_cfg.timer_num = BL_TIMER;
    timer_cfg.freq_hz = BL_FREQ_HZ;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = LCD_PIN_BL;
    channel_cfg.speed_mode = BL_MODE;
    channel_cfg.channel = BL_CHANNEL;
    channel_cfg.timer_sel = BL_TIMER;
    channel_cfg.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
}

void lcd_init(esp_lcd_panel_io_handle_t &io, esp_lcd_panel_handle_t &panel)
{
    backlight_init();

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = LCD_PIN_MOSI;
    bus_cfg.miso_io_num = GPIO_NUM_NC;
    bus_cfg.sclk_io_num = LCD_PIN_SCLK;
    bus_cfg.quadwp_io_num = GPIO_NUM_NC;
    bus_cfg.quadhd_io_num = GPIO_NUM_NC;
    bus_cfg.max_transfer_sz = LCD_H_RES * LCD_DRAW_LINES * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = LCD_PIN_CS;
    io_cfg.dc_gpio_num = LCD_PIN_DC;
    io_cfg.spi_mode = 3;
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(LCD_HOST), &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = LCD_PIN_RST;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
}

}

lv_display_t *init()
{
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    lcd_init(io, panel);

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = io;
    disp_cfg.panel_handle = panel;
    disp_cfg.buffer_size = LCD_H_RES * LCD_DRAW_LINES;
    disp_cfg.double_buffer = true;
    disp_cfg.hres = LCD_H_RES;
    disp_cfg.vres = LCD_V_RES;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.swap_bytes = true;
    disp_cfg.flags.sw_rotate = true;
    return lvgl_port_add_disp(&disp_cfg);
}

void set_brightness(uint8_t percent)
{
    const uint32_t p = percent > 100 ? 100 : percent;
    ESP_ERROR_CHECK(ledc_set_duty_and_update(BL_MODE, BL_CHANNEL, BL_DUTY_MAX * p / 100, 0));
}

}
