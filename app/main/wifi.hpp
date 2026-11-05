#pragma once

#include "esp_err.h"

namespace wifi {

enum class Status {
    Connecting,
    Connected,
    Disconnected,
};

using StatusCallback = void (*)(Status status, const char *detail, void *ctx);

esp_err_t start(StatusCallback cb, void *ctx);

}
