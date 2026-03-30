#pragma once

#include <atomic>
#include <cstdio>
#include <string>

extern "C" {
#include "esp_http_server.h"
#include "esp_netif.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
}

namespace fslogger {

struct AppState {
  SemaphoreHandle_t mutex = nullptr;
  QueueHandle_t uart_queue = nullptr;
  FILE *active_log = nullptr;
  std::string active_log_fs_path;
  std::string active_log_web_path;
  std::string ap_ip;
  httpd_handle_t http_server = nullptr;
  esp_netif_t *ap_netif = nullptr;
  std::atomic<bool> web_started{false};
  std::atomic<int64_t> last_uart_activity_us{0};
};

extern AppState g_state;

class ScopedLock {
 public:
  explicit ScopedLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
  }

  ~ScopedLock() {
    xSemaphoreGive(mutex_);
  }

 private:
  SemaphoreHandle_t mutex_;
};

}  // namespace fslogger
