#include "app_config.h"
#include "app_state.h"
#include "log_storage.h"
#include "system_init.h"
#include "uart_service.h"

extern "C" {
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
}

extern "C" void app_main(void) {
  fslogger::g_state.mutex = xSemaphoreCreateMutex();
  if (fslogger::g_state.mutex == nullptr) {
    ESP_LOGE(fslogger::kTag, "Failed to create mutex");
    return;
  }

  fslogger::g_state.last_uart_activity_us.store(fslogger::now_us());

  ESP_ERROR_CHECK(fslogger::init_nvs());
  ESP_ERROR_CHECK(esp_netif_init());

  const esp_err_t event_loop_err = esp_event_loop_create_default();
  if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(event_loop_err);
  }

  ESP_ERROR_CHECK(fslogger::mount_littlefs());

  {
    fslogger::ScopedLock lock(fslogger::g_state.mutex);
    fslogger::ensure_directory(fslogger::logs_dir_fs_path());
    fslogger::cleanup_truncated_logs_locked();
    if (!fslogger::create_new_active_log_locked()) {
      ESP_LOGE(fslogger::kTag, "Failed to prepare the initial log file");
      return;
    }
  }

  ESP_ERROR_CHECK(fslogger::init_uart());

  ESP_LOGI(fslogger::kTag, "UART logger ready on UART%d RX=%d baud=%d", APP_UART_PORT,
           APP_UART_RX_PIN, APP_UART_BAUD);
  ESP_LOGI(fslogger::kTag, "The web UI will start after %d ms of UART inactivity",
           APP_WEB_START_DELAY_MS);

  xTaskCreate(fslogger::uart_rx_task, "uart_rx_task", 4096, nullptr, 10, nullptr);
  xTaskCreate(fslogger::web_starter_task, "web_starter_task", 4096, nullptr, 4, nullptr);
}
