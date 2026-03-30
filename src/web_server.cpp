#include "web_server.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

extern "C" {
#include "esp_littlefs.h"
#include "esp_log.h"
}

#include "app_config.h"
#include "app_state.h"
#include "log_storage.h"

namespace fslogger {
namespace {

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool ends_with(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

char from_hex(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<char>(value - '0');
  }

  value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  if (value >= 'a' && value <= 'f') {
    return static_cast<char>(value - 'a' + 10);
  }

  return 0;
}

std::string url_decode(const std::string &value) {
  std::string decoded;
  decoded.reserve(value.size());

  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const char high = from_hex(value[i + 1]);
      const char low = from_hex(value[i + 2]);
      decoded.push_back(static_cast<char>((high << 4) | low));
      i += 2;
    } else if (value[i] == '+') {
      decoded.push_back(' ');
    } else {
      decoded.push_back(value[i]);
    }
  }

  return decoded;
}

std::string json_escape(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);

  for (char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }

  return escaped;
}

bool read_query_value(httpd_req_t *req, const char *key, std::string *out) {
  const size_t query_length = httpd_req_get_url_query_len(req);
  if (query_length == 0) {
    return false;
  }

  std::string query(query_length + 1, '\0');
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
    return false;
  }

  char buffer[512] = {};
  if (httpd_query_key_value(query.c_str(), key, buffer, sizeof(buffer)) != ESP_OK) {
    return false;
  }

  *out = url_decode(buffer);
  return true;
}

void set_no_cache(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
}

const char *content_type_for_path(const std::string &path) {
  if (ends_with(path, ".html")) {
    return "text/html";
  }
  if (ends_with(path, ".css")) {
    return "text/css";
  }
  if (ends_with(path, ".js")) {
    return "application/javascript";
  }
  if (ends_with(path, ".json")) {
    return "application/json";
  }
  if (ends_with(path, ".svg")) {
    return "image/svg+xml";
  }
  if (ends_with(path, ".txt")) {
    return "text/plain";
  }
  if (ends_with(path, ".csv")) {
    return "text/csv";
  }
  return "application/octet-stream";
}

esp_err_t send_file(httpd_req_t *req, const std::string &fs_path) {
  FILE *file = std::fopen(fs_path.c_str(), "rb");
  if (file == nullptr) {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
  }

  std::array<char, kHttpBufferSize> buffer = {};
  while (true) {
    const size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
    if (read > 0 && httpd_resp_send_chunk(req, buffer.data(), read) != ESP_OK) {
      std::fclose(file);
      httpd_resp_sendstr_chunk(req, nullptr);
      return ESP_FAIL;
    }
    if (read < buffer.size()) {
      break;
    }
  }

  std::fclose(file);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_status(httpd_req_t *req) {
  set_no_cache(req);
  httpd_resp_set_type(req, "application/json");

  size_t total_bytes = 0;
  size_t used_bytes = 0;
  if (esp_littlefs_info(kPartitionLabel, &total_bytes, &used_bytes) != ESP_OK) {
    total_bytes = 0;
    used_bytes = 0;
  }

  std::string active_log;
  std::string ap_ip;
  {
    ScopedLock lock(g_state.mutex);
    active_log = g_state.active_log_web_path;
    ap_ip = g_state.ap_ip;
  }

  char response[512];
  std::snprintf(
      response, sizeof(response),
      "{\"activeLog\":\"%s\",\"webStarted\":%s,\"apSsid\":\"%s\",\"apIp\":\"%s\","
      "\"uartPort\":%d,\"uartRxPin\":%d,\"uartTxPin\":%d,\"uartBaud\":%d,"
      "\"storageTotal\":%u,\"storageUsed\":%u}",
      json_escape(active_log).c_str(), g_state.web_started.load() ? "true" : "false", kApSsid,
      json_escape(ap_ip).c_str(), APP_UART_PORT, APP_UART_RX_PIN, APP_UART_TX_PIN,
      APP_UART_BAUD, static_cast<unsigned int>(total_bytes),
      static_cast<unsigned int>(used_bytes));

  return httpd_resp_sendstr(req, response);
}

esp_err_t handle_list(httpd_req_t *req) {
  std::string requested_dir;
  if (!read_query_value(req, "dir", &requested_dir)) {
    requested_dir = kLogsDirWeb;
  }

  if (requested_dir == "/logs/csv/") {
    requested_dir = kLogsDirWeb;
  }

  if (requested_dir != kLogsDirWeb) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Only the log directory is allowed");
  }

  std::vector<ListedFile> files;
  {
    ScopedLock lock(g_state.mutex);
    files = list_log_files_locked();
  }

  set_no_cache(req);
  httpd_resp_set_type(req, "application/json");

  std::string json = "[";
  for (size_t i = 0; i < files.size(); ++i) {
    if (i > 0) {
      json += ",";
    }
    json += "{\"name\":\"" + json_escape(files[i].name) + "\",";
    json += "\"path\":\"" + json_escape(files[i].web_path) + "\",";
    json += "\"size\":" + std::to_string(files[i].size) + ",";
    json += "\"active\":";
    json += files[i].active ? "true" : "false";
    json += "}";
  }
  json += "]";

  return httpd_resp_sendstr(req, json.c_str());
}

esp_err_t handle_delete_logs(httpd_req_t *req) {
  std::string requested_dir;
  if (!read_query_value(req, "dir", &requested_dir)) {
    requested_dir = kLogsDirWeb;
  }

  if (requested_dir == "/logs/csv/") {
    requested_dir = kLogsDirWeb;
  }

  if (requested_dir != kLogsDirWeb) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Only the log directory is allowed");
  }

  {
    ScopedLock lock(g_state.mutex);
    close_active_log_locked(true);

    DIR *dir = opendir(logs_dir_fs_path().c_str());
    if (dir != nullptr) {
      while (dirent *entry = readdir(dir)) {
        if (!starts_with(entry->d_name, kLogPrefix) ||
            !ends_with(entry->d_name, kLogExtension)) {
          continue;
        }

        const std::string file_path = logs_dir_fs_path() + "/" + entry->d_name;
        ::remove(file_path.c_str());
      }
      closedir(dir);
    }

    if (!create_new_active_log_locked()) {
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Failed to create a new active log");
    }
  }

  return handle_status(req);
}

esp_err_t handle_delete_file(httpd_req_t *req) {
  std::string web_path;
  if (!read_query_value(req, "path", &web_path) || !is_log_web_path(web_path)) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid log file path");
  }

  const std::string fs_path = web_to_fs_path(web_path);
  if (!file_info(fs_path)) {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Log file not found");
  }

  {
    ScopedLock lock(g_state.mutex);
    if (web_path == g_state.active_log_web_path) {
      close_active_log_locked(true);
      if (::remove(fs_path.c_str()) != 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Failed to remove active log file");
      }
      if (!create_new_active_log_locked()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Failed to rotate the active log");
      }
    } else if (::remove(fs_path.c_str()) != 0) {
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Failed to remove log file");
    }
  }

  return handle_status(req);
}

esp_err_t handle_put_file(httpd_req_t *req) {
  std::string web_path;
  if (!read_query_value(req, "path", &web_path) || !is_log_web_path(web_path)) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid log file path");
  }

  const std::string fs_path = web_to_fs_path(web_path);
  if (!ensure_directory(parent_directory(fs_path))) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Failed to create parent directory");
  }

  ScopedLock lock(g_state.mutex);

  const bool rewrite_active = (web_path == g_state.active_log_web_path);
  if (rewrite_active) {
    close_active_log_locked(false);
  }

  FILE *file = std::fopen(fs_path.c_str(), "w");
  if (file == nullptr) {
    if (rewrite_active) {
      reopen_active_log_locked();
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Failed to open log file for writing");
  }

  std::array<char, kHttpBufferSize> buffer = {};
  int remaining = req->content_len;
  while (remaining > 0) {
    const int request_size = std::min(remaining, static_cast<int>(buffer.size()));
    const int received = httpd_req_recv(req, buffer.data(), request_size);

    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (received <= 0) {
      std::fclose(file);
      ::remove(fs_path.c_str());
      if (rewrite_active) {
        reopen_active_log_locked();
      }
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Failed to receive request body");
    }

    if (std::fwrite(buffer.data(), 1, received, file) != static_cast<size_t>(received)) {
      std::fclose(file);
      ::remove(fs_path.c_str());
      if (rewrite_active) {
        reopen_active_log_locked();
      }
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Failed to write file body");
    }

    remaining -= received;
  }

  std::fflush(file);
  std::fclose(file);

  if (rewrite_active && !reopen_active_log_locked()) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Failed to reopen active log");
  }

  return handle_status(req);
}

esp_err_t handle_static(httpd_req_t *req) {
  std::string web_path = req->uri;
  if (web_path == "/") {
    web_path = "/index.html";
  }

  if (!is_safe_web_path(web_path)) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
  }

  std::string fs_path = web_to_fs_path(web_path);
  struct stat st = {};
  if (::stat(fs_path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
    if (!ends_with(web_path, "/")) {
      web_path += "/";
    }
    web_path += "index.html";
    fs_path = web_to_fs_path(web_path);
  }

  {
    ScopedLock lock(g_state.mutex);
    if (web_path == g_state.active_log_web_path && g_state.active_log != nullptr) {
      std::fflush(g_state.active_log);
    }
  }

  if (::stat(fs_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }

  httpd_resp_set_type(req, content_type_for_path(fs_path));
  if (is_log_web_path(web_path)) {
    set_no_cache(req);
  }

  return send_file(req, fs_path);
}

}  // namespace

httpd_handle_t start_http_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;
  config.stack_size = 8192;
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start() failed");
    return nullptr;
  }

  const httpd_uri_t status_handler = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = handle_status,
      .user_ctx = nullptr,
  };
  const httpd_uri_t list_handler = {
      .uri = "/api/list",
      .method = HTTP_GET,
      .handler = handle_list,
      .user_ctx = nullptr,
  };
  const httpd_uri_t put_file_handler = {
      .uri = "/api/file",
      .method = HTTP_PUT,
      .handler = handle_put_file,
      .user_ctx = nullptr,
  };
  const httpd_uri_t delete_file_handler = {
      .uri = "/api/file",
      .method = HTTP_DELETE,
      .handler = handle_delete_file,
      .user_ctx = nullptr,
  };
  const httpd_uri_t delete_logs_handler = {
      .uri = "/api/logs",
      .method = HTTP_DELETE,
      .handler = handle_delete_logs,
      .user_ctx = nullptr,
  };
  const httpd_uri_t static_handler = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = handle_static,
      .user_ctx = nullptr,
  };

  httpd_register_uri_handler(server, &status_handler);
  httpd_register_uri_handler(server, &list_handler);
  httpd_register_uri_handler(server, &put_file_handler);
  httpd_register_uri_handler(server, &delete_file_handler);
  httpd_register_uri_handler(server, &delete_logs_handler);
  httpd_register_uri_handler(server, &static_handler);

  return server;
}

}  // namespace fslogger
