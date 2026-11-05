#pragma once

#include "esp_err.h"

namespace button {

using PressCallback = void (*)(void *ctx);

esp_err_t start(PressCallback cb, void *ctx);

}
