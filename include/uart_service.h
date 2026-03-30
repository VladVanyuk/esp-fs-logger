#pragma once

extern "C" {
#include "esp_err.h"
}

namespace fslogger {

esp_err_t init_uart();
void uart_rx_task(void *unused);
void web_starter_task(void *unused);

}  // namespace fslogger
