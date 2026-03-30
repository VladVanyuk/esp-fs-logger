#include "uart_service.h"

#include <algorithm>
#include <array>

extern "C" {
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"
}

#include "app_config.h"
#include "app_state.h"
#include "log_storage.h"
#include "network.h"

namespace fslogger {

esp_err_t init_uart() {
  uart_config_t uart_config = {};
  uart_config.baud_rate = APP_UART_BAUD;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_param_config(kUartPort, &uart_config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "uart_param_config failed: %s", esp_err_to_name(err));
    return err;
  }

  const int tx_pin = APP_UART_TX_PIN < 0 ? UART_PIN_NO_CHANGE : APP_UART_TX_PIN;
  err = uart_set_pin(kUartPort, tx_pin, APP_UART_RX_PIN, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "uart_set_pin failed: %s", esp_err_to_name(err));
    return err;
  }

  err = uart_driver_install(kUartPort, kUartRxBufferSize, 0, kUartEventQueueDepth,
                            &g_state.uart_queue, 0);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "uart_driver_install failed: %s", esp_err_to_name(err));
    return err;
  }

  return ESP_OK;
}

void uart_rx_task(void *unused) {
  std::array<uint8_t, 256> buffer = {};
  uart_event_t event = {};

  while (true) {
    if (xQueueReceive(g_state.uart_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    switch (event.type) {
      case UART_DATA: {
        int remaining = static_cast<int>(event.size);
        while (remaining > 0) {
          const int chunk = std::min(remaining, static_cast<int>(buffer.size()));
          const int read = uart_read_bytes(kUartPort, buffer.data(), chunk, pdMS_TO_TICKS(100));
          if (read <= 0) {
            break;
          }

          g_state.last_uart_activity_us.store(now_us());
          if (!append_to_active_log(buffer.data(), static_cast<size_t>(read))) {
            ESP_LOGE(kTag, "Failed to append %d UART bytes", read);
          }
          remaining -= read;
        }
        break;
      }

      case UART_FIFO_OVF:
      case UART_BUFFER_FULL:
        ESP_LOGW(kTag, "UART buffer overflow, flushing input");
        uart_flush_input(kUartPort);
        xQueueReset(g_state.uart_queue);
        break;

      case UART_PARITY_ERR:
        ESP_LOGW(kTag, "UART parity error");
        break;

      case UART_FRAME_ERR:
        ESP_LOGW(kTag, "UART frame error");
        break;

      default:
        break;
    }
  }
}

void web_starter_task(void *unused) {
  while (true) {
    if (!g_state.web_started.load()) {
      const int64_t idle_us = now_us() - g_state.last_uart_activity_us.load();
      if (idle_us >= static_cast<int64_t>(APP_WEB_START_DELAY_MS) * 1000LL) {
        if (start_web_stack()) {
          vTaskDelete(nullptr);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

}  // namespace fslogger
