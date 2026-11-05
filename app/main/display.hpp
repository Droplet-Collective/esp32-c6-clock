#pragma once

#include <cstdint>

#include "lvgl.h"

namespace display {

lv_display_t *init();
void set_brightness(uint8_t percent);

}
