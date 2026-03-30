#include "network.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
}

#include "app_config.h"
#include "app_state.h"
#include "web_server.h"

namespace fslogger {

bool start_web_stack() {
  if (g_state.web_started.load()) {
    return true;
  }

  if (g_state.ap_netif == nullptr) {
    g_state.ap_netif = esp_netif_create_default_wifi_ap();
    if (g_state.ap_netif == nullptr) {
      ESP_LOGE(kTag, "esp_netif_create_default_wifi_ap() failed");
      return false;
    }
  }

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  wifi_config_t ap_config = {};
  std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), kApSsid, sizeof(ap_config.ap.ssid));
  ap_config.ap.ssid_len = std::strlen(kApSsid);
  ap_config.ap.channel = 1;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;
  ap_config.ap.max_connection = APP_MAX_AP_CLIENTS;
  ap_config.ap.beacon_interval = 100;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_netif_ip_info_t ip_info = {};
  ESP_ERROR_CHECK(esp_netif_get_ip_info(g_state.ap_netif, &ip_info));

  char ip_buffer[16];
  std::snprintf(ip_buffer, sizeof(ip_buffer), IPSTR, IP2STR(&ip_info.ip));

  {
    ScopedLock lock(g_state.mutex);
    g_state.ap_ip = ip_buffer;
    g_state.http_server = start_http_server();
    if (g_state.http_server == nullptr) {
      return false;
    }
  }

  g_state.web_started.store(true);
  ESP_LOGI(kTag, "Access point %s started at http://%s/", kApSsid, ip_buffer);
  return true;
}

}  // namespace fslogger
