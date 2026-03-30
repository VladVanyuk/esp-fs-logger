#pragma once

extern "C" {
#include "esp_err.h"
}

namespace fslogger {

esp_err_t init_nvs();

}  // namespace fslogger
