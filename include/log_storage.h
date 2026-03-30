#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "esp_err.h"
}

namespace fslogger {

struct ListedFile {
  std::string name;
  std::string web_path;
  size_t size = 0;
  uint32_t index = 0;
  bool active = false;
};

std::string logs_dir_fs_path();
bool is_safe_web_path(const std::string &path);
bool is_log_web_path(const std::string &path);
std::string web_to_fs_path(const std::string &web_path);
std::string parent_directory(const std::string &path);
bool ensure_directory(const std::string &path);
bool file_info(const std::string &path, size_t *size_out = nullptr);
void close_active_log_locked(bool clear_paths);
void cleanup_truncated_logs_locked();
bool create_new_active_log_locked();
bool reopen_active_log_locked();
bool append_to_active_log(const uint8_t *data, size_t length);
std::vector<ListedFile> list_log_files_locked();
esp_err_t mount_littlefs();

}  // namespace fslogger
