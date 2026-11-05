#pragma once

#include "esp_err.h"

namespace time_sync {

esp_err_t start();
bool is_synced();

}
