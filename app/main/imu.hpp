#pragma once

#include "esp_err.h"

namespace imu {

enum class Orientation {
    Up,
    Right,
    Down,
    Left,
    Flat,
};

using OrientationCallback = void (*)(Orientation orientation, void *ctx);

esp_err_t start(OrientationCallback cb, void *ctx);
const char *name(Orientation orientation);

}
