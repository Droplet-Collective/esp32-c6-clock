#include "imu.hpp"

#include <cstdint>
#include <cstdlib>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace imu {
namespace {

constexpr i2c_port_num_t IMU_I2C_PORT = 0;
constexpr gpio_num_t IMU_PIN_SDA = GPIO_NUM_8;
constexpr gpio_num_t IMU_PIN_SCL = GPIO_NUM_7;
constexpr gpio_num_t IMU_PIN_INT1 = GPIO_NUM_10;
constexpr uint16_t IMU_I2C_ADDR = 0x6B;
constexpr uint32_t IMU_I2C_HZ = 400000;
constexpr int IMU_I2C_TIMEOUT_MS = 100;

constexpr uint8_t REG_WHO_AM_I = 0x00;
constexpr uint8_t REG_REVISION_ID = 0x01;
constexpr uint8_t REG_CTRL1 = 0x02;
constexpr uint8_t REG_CTRL2 = 0x03;
constexpr uint8_t REG_CTRL7 = 0x08;
constexpr uint8_t REG_CTRL8 = 0x09;
constexpr uint8_t REG_CTRL9 = 0x0A;
constexpr uint8_t REG_CAL1_L = 0x0B;
constexpr uint8_t REG_STATUSINT = 0x2D;
constexpr uint8_t REG_STATUS1 = 0x2F;
constexpr uint8_t REG_AX_L = 0x35;
constexpr uint8_t REG_RESET_STATUS = 0x4D;
constexpr uint8_t REG_RESET = 0x60;

constexpr uint8_t WHO_AM_I_VALUE = 0x05;
constexpr uint8_t RESET_CMD = 0xB0;
constexpr uint8_t RESET_DONE = 0x80;
constexpr uint8_t CTRL1_ADDR_AI = 0x40;
constexpr uint8_t CTRL1_INT1_EN = 0x08;
constexpr uint8_t CTRL2_ACC_4G_31HZ = 0x18;
constexpr uint8_t CTRL7_ACC_EN = 0x01;
constexpr uint8_t CTRL8_HANDSHAKE_STAT = 0x80;
constexpr uint8_t CTRL8_ACTIVITY_INT1 = 0x40;
constexpr uint8_t CTRL8_NO_MOTION_EN = 0x04;
constexpr uint8_t CTRL8_ANY_MOTION_EN = 0x02;
constexpr uint8_t CTRL9_CMD_ACK = 0x00;
constexpr uint8_t CTRL9_CMD_CONF_MOTION = 0x0E;
constexpr uint8_t STATUSINT_CMD_DONE = 0x80;
constexpr uint8_t STATUS1_ANY_MOTION = 0x20;
constexpr uint8_t STATUS1_NO_MOTION = 0x40;

constexpr uint8_t ANY_MOTION_THR = 1;
constexpr uint8_t ANY_MOTION_WINDOW = 1;
constexpr uint8_t NO_MOTION_THR = 2;
constexpr uint8_t NO_MOTION_WINDOW = 16;
constexpr uint8_t MOTION_MODE_CTRL = 0xF7;

constexpr int ACC_LSB_PER_G = 8192;
constexpr int ORIENT_MIN_G = ACC_LSB_PER_G * 6 / 10;

constexpr const char *TAG = "imu";

i2c_master_dev_handle_t s_dev;
TaskHandle_t s_task;
OrientationCallback s_cb;
void *s_cb_ctx;

esp_err_t reg_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, IMU_I2C_TIMEOUT_MS);
}

esp_err_t reg_write(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), IMU_I2C_TIMEOUT_MS);
}

esp_err_t ctrl9_command(uint8_t cmd)
{
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL9, cmd), TAG, "CTRL9 write");
    for (int i = 0; i < 100; i++) {
        uint8_t st;
        ESP_RETURN_ON_ERROR(reg_read(REG_STATUSINT, &st, 1), TAG, "STATUSINT read");
        if (st & STATUSINT_CMD_DONE) {
            return reg_write(REG_CTRL9, CTRL9_CMD_ACK);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGE(TAG, "CTRL9 command 0x%02x timed out", cmd);
    return ESP_ERR_TIMEOUT;
}

esp_err_t write_motion_params(const uint8_t (&cal)[8])
{
    uint8_t buf[9] = {REG_CAL1_L};
    for (int i = 0; i < 8; i++) {
        buf[i + 1] = cal[i];
    }
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, buf, sizeof(buf), IMU_I2C_TIMEOUT_MS), TAG, "CAL write");
    return ctrl9_command(CTRL9_CMD_CONF_MOTION);
}

esp_err_t sensor_init()
{
    uint8_t id = 0;
    uint8_t rev = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_WHO_AM_I, &id, 1), TAG, "WHO_AM_I read");
    ESP_RETURN_ON_FALSE(id == WHO_AM_I_VALUE, ESP_ERR_NOT_FOUND, TAG, "unexpected WHO_AM_I 0x%02x", id);
    ESP_RETURN_ON_ERROR(reg_read(REG_REVISION_ID, &rev, 1), TAG, "REVISION_ID read");
    ESP_LOGI(TAG, "QMI8658 found (revision 0x%02x)", rev);

    ESP_RETURN_ON_ERROR(reg_write(REG_RESET, RESET_CMD), TAG, "soft reset");
    uint8_t reset_status = 0;
    for (int i = 0; i < 50 && reset_status != RESET_DONE; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
        reg_read(REG_RESET_STATUS, &reset_status, 1);
    }
    ESP_RETURN_ON_FALSE(reset_status == RESET_DONE, ESP_ERR_TIMEOUT, TAG, "soft reset timed out");

    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL1, CTRL1_ADDR_AI), TAG, "CTRL1");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL7, 0), TAG, "CTRL7");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL8, CTRL8_HANDSHAKE_STAT), TAG, "CTRL8");

    const uint8_t thresholds[8] = {
        ANY_MOTION_THR, ANY_MOTION_THR, ANY_MOTION_THR,
        NO_MOTION_THR, NO_MOTION_THR, NO_MOTION_THR,
        MOTION_MODE_CTRL, 0x01,
    };
    const uint8_t windows[8] = {
        ANY_MOTION_WINDOW, NO_MOTION_WINDOW,
        0, 0, 0, 0,
        0, 0x02,
    };
    ESP_RETURN_ON_ERROR(write_motion_params(thresholds), TAG, "motion thresholds");
    ESP_RETURN_ON_ERROR(write_motion_params(windows), TAG, "motion windows");

    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL2, CTRL2_ACC_4G_31HZ), TAG, "CTRL2");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL8, CTRL8_HANDSHAKE_STAT | CTRL8_ACTIVITY_INT1 | CTRL8_NO_MOTION_EN), TAG, "CTRL8");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL7, CTRL7_ACC_EN), TAG, "CTRL7");
    return reg_write(REG_CTRL1, CTRL1_ADDR_AI | CTRL1_INT1_EN);
}

bool classify(int sx, int sy, int sz, Orientation &out)
{
    const int x = abs(sx);
    const int y = abs(sy);
    const int z = abs(sz);
    if (z >= x && z >= y) {
        if (z < ORIENT_MIN_G) {
            return false;
        }
        out = Orientation::Flat;
    } else if (x >= y) {
        if (x < ORIENT_MIN_G) {
            return false;
        }
        out = sx < 0 ? Orientation::Right : Orientation::Left;
    } else {
        if (y < ORIENT_MIN_G) {
            return false;
        }
        out = sy > 0 ? Orientation::Up : Orientation::Down;
    }
    return true;
}

void on_rest(bool &has_orientation, Orientation &current)
{
    uint8_t raw[6];
    if (reg_read(REG_AX_L, raw, sizeof(raw)) != ESP_OK) {
        ESP_LOGW(TAG, "accel read failed");
        return;
    }
    const auto ax = static_cast<int16_t>(raw[0] | raw[1] << 8);
    const auto ay = static_cast<int16_t>(raw[2] | raw[3] << 8);
    const auto az = static_cast<int16_t>(raw[4] | raw[5] << 8);

    Orientation orientation;
    if (!classify(-ax, ay, -az, orientation)) {
        ESP_LOGI(TAG, "at rest, acc=(%d, %d, %d): ambiguous", ax, ay, az);
        return;
    }
    ESP_LOGI(TAG, "at rest, acc=(%d, %d, %d): %s", ax, ay, az, name(orientation));
    if (!has_orientation || orientation != current) {
        has_orientation = true;
        current = orientation;
        s_cb(orientation, s_cb_ctx);
    }
}

void int1_isr(void *)
{
    BaseType_t woken = pdFALSE;
    gpio_intr_disable(IMU_PIN_INT1);
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

void imu_task(void *)
{
    bool waiting_for_rest = true;
    bool has_orientation = false;
    Orientation current = Orientation::Flat;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t status1 = 0;
        if (reg_read(REG_STATUS1, &status1, 1) == ESP_OK) {
            const uint8_t base = CTRL8_HANDSHAKE_STAT | CTRL8_ACTIVITY_INT1;
            if (waiting_for_rest && (status1 & STATUS1_NO_MOTION)) {
                on_rest(has_orientation, current);
                waiting_for_rest = reg_write(REG_CTRL8, base | CTRL8_ANY_MOTION_EN) != ESP_OK;
            } else if (!waiting_for_rest && (status1 & STATUS1_ANY_MOTION)) {
                ESP_LOGD(TAG, "moving");
                waiting_for_rest = reg_write(REG_CTRL8, base | CTRL8_NO_MOTION_EN) == ESP_OK;
            }
        }
        gpio_intr_enable(IMU_PIN_INT1);
    }
}

}

esp_err_t start(OrientationCallback cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(cb != nullptr && s_task == nullptr, ESP_ERR_INVALID_STATE, TAG, "invalid call");
    s_cb = cb;
    s_cb_ctx = ctx;

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = IMU_I2C_PORT;
    bus_cfg.sda_io_num = IMU_PIN_SDA;
    bus_cfg.scl_io_num = IMU_PIN_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "I2C bus");

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = IMU_I2C_ADDR;
    dev_cfg.scl_speed_hz = IMU_I2C_HZ;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "I2C device");

    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = 1ULL << IMU_PIN_INT1;
    int_cfg.mode = GPIO_MODE_INPUT;
    int_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    int_cfg.intr_type = GPIO_INTR_POSEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "INT1 GPIO");

    ESP_RETURN_ON_ERROR(sensor_init(), TAG, "sensor init");
    ESP_RETURN_ON_FALSE(xTaskCreate(imu_task, "imu", 3072, nullptr, 5, &s_task) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "ISR service");
    return gpio_isr_handler_add(IMU_PIN_INT1, int1_isr, nullptr);
}

const char *name(Orientation orientation)
{
    switch (orientation) {
    case Orientation::Up:    return "up";
    case Orientation::Right: return "right";
    case Orientation::Down:  return "down";
    case Orientation::Left:  return "left";
    case Orientation::Flat:  return "flat";
    }
    return "?";
}

}
