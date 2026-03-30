#pragma once

extern "C" {
#include "esp_http_server.h"
}

namespace fslogger {

httpd_handle_t start_http_server();

}  // namespace fslogger
