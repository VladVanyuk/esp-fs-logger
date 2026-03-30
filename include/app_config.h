#pragma once

#include <cstdint>

extern "C" {
#include "driver/uart.h"
#include "esp_timer.h"
}

#ifndef APP_UART_PORT
#define APP_UART_PORT 1
#endif

#ifndef APP_UART_RX_PIN
#define APP_UART_RX_PIN 18
#endif

#ifndef APP_UART_TX_PIN
#define APP_UART_TX_PIN -1
#endif

#ifndef APP_UART_BAUD
#define APP_UART_BAUD 38400
#endif

#ifndef APP_WEB_START_DELAY_MS
#define APP_WEB_START_DELAY_MS 10000
#endif

#ifndef APP_MAX_AP_CLIENTS
#define APP_MAX_AP_CLIENTS 4
#endif

namespace fslogger {

constexpr char kTag[] = "fslogger";
constexpr char kApSsid[] = "hcLogs";
constexpr char kMountPoint[] = "/littlefs";
constexpr char kPartitionLabel[] = "littlefs";
constexpr char kLogsDirWeb[] = "/logs/csv";
constexpr char kLogPrefix[] = "logs_";
constexpr char kLogExtension[] = ".txt";
constexpr char kLogHeader[] =
    "state\taccel\tfreefall\tres\tx\ty\tz\tpin_state\tbat\tbf_d\tToF_signal\tsys_time\n";
constexpr int kUartEventQueueDepth = 20;
constexpr int kUartRxBufferSize = 4096;
constexpr int kHttpBufferSize = 1024;
constexpr uart_port_t kUartPort = static_cast<uart_port_t>(APP_UART_PORT);

inline int64_t now_us() {
  return esp_timer_get_time();
}

}  // namespace fslogger
