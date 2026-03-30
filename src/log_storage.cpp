#include "log_storage.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
}

#include "app_config.h"
#include "app_state.h"

namespace fslogger {
namespace {

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool ends_with(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

uint32_t extract_log_index(const std::string &file_name) {
  unsigned int index = 0;
  if (std::sscanf(file_name.c_str(), "logs_%u.txt", &index) == 1) {
    return static_cast<uint32_t>(index);
  }
  return 0;
}

std::string make_log_web_path(uint32_t index) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%s/%s%lu%s", kLogsDirWeb, kLogPrefix,
                static_cast<unsigned long>(index), kLogExtension);
  return buffer;
}

uint32_t next_log_index_locked() {
  uint32_t max_index = 0;
  bool found_any = false;

  DIR *dir = opendir(logs_dir_fs_path().c_str());
  if (dir == nullptr) {
    return 0;
  }

  while (dirent *entry = readdir(dir)) {
    if (!starts_with(entry->d_name, kLogPrefix) || !ends_with(entry->d_name, kLogExtension)) {
      continue;
    }

    max_index = std::max(max_index, extract_log_index(entry->d_name));
    found_any = true;
  }

  closedir(dir);
  return found_any ? (max_index + 1) : 0;
}

}  // namespace

std::string logs_dir_fs_path() {
  return std::string(kMountPoint) + kLogsDirWeb;
}

bool is_safe_web_path(const std::string &path) {
  return !path.empty() && path.front() == '/' && path.find("..") == std::string::npos &&
         path.find('\\') == std::string::npos;
}

bool is_log_web_path(const std::string &path) {
  return is_safe_web_path(path) && starts_with(path, kLogsDirWeb);
}

std::string web_to_fs_path(const std::string &web_path) {
  return std::string(kMountPoint) + web_path;
}

std::string parent_directory(const std::string &path) {
  const size_t pos = path.find_last_of('/');
  if (pos == std::string::npos || pos == 0) {
    return "/";
  }
  return path.substr(0, pos);
}

bool ensure_directory(const std::string &path) {
  if (path.empty()) {
    return false;
  }

  std::string current;
  for (size_t i = 1; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      current = path.substr(0, i);
      if (current.empty()) {
        continue;
      }
      if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(kTag, "mkdir(%s) failed: %s", current.c_str(), strerror(errno));
        return false;
      }
    }
  }

  return true;
}

bool file_info(const std::string &path, size_t *size_out) {
  struct stat st = {};
  if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    return false;
  }
  if (size_out != nullptr) {
    *size_out = static_cast<size_t>(st.st_size);
  }
  return true;
}

void close_active_log_locked(bool clear_paths) {
  if (g_state.active_log != nullptr) {
    std::fflush(g_state.active_log);
    std::fclose(g_state.active_log);
    g_state.active_log = nullptr;
  }

  if (clear_paths) {
    g_state.active_log_fs_path.clear();
    g_state.active_log_web_path.clear();
  }
}

void cleanup_truncated_logs_locked() {
  DIR *dir = opendir(logs_dir_fs_path().c_str());
  if (dir == nullptr) {
    return;
  }

  const size_t min_valid_size = std::strlen(kLogHeader);
  while (dirent *entry = readdir(dir)) {
    if (!starts_with(entry->d_name, kLogPrefix) || !ends_with(entry->d_name, kLogExtension)) {
      continue;
    }

    const std::string file_path = logs_dir_fs_path() + "/" + entry->d_name;
    size_t size = 0;
    if (file_info(file_path, &size) && size <= min_valid_size) {
      ESP_LOGW(kTag, "Removing truncated log %s", file_path.c_str());
      ::remove(file_path.c_str());
    }
  }

  closedir(dir);
}

bool create_new_active_log_locked() {
  if (!ensure_directory(logs_dir_fs_path())) {
    return false;
  }

  const std::string web_path = make_log_web_path(next_log_index_locked());
  const std::string fs_path = web_to_fs_path(web_path);

  FILE *file = std::fopen(fs_path.c_str(), "w");
  if (file == nullptr) {
    ESP_LOGE(kTag, "Failed to create %s: %s", fs_path.c_str(), strerror(errno));
    return false;
  }

  const size_t header_len = std::strlen(kLogHeader);
  if (std::fwrite(kLogHeader, 1, header_len, file) != header_len) {
    ESP_LOGE(kTag, "Failed to write header to %s", fs_path.c_str());
    std::fclose(file);
    ::remove(fs_path.c_str());
    return false;
  }

  std::fflush(file);
  g_state.active_log = file;
  g_state.active_log_fs_path = fs_path;
  g_state.active_log_web_path = web_path;
  ESP_LOGI(kTag, "Active log file: %s", web_path.c_str());
  return true;
}

bool reopen_active_log_locked() {
  if (g_state.active_log != nullptr || g_state.active_log_fs_path.empty()) {
    return g_state.active_log != nullptr;
  }

  g_state.active_log = std::fopen(g_state.active_log_fs_path.c_str(), "a");
  if (g_state.active_log == nullptr) {
    ESP_LOGE(kTag, "Failed to reopen %s", g_state.active_log_fs_path.c_str());
    return false;
  }

  return true;
}

bool append_to_active_log(const uint8_t *data, size_t length) {
  ScopedLock lock(g_state.mutex);

  if (g_state.active_log == nullptr && !create_new_active_log_locked()) {
    return false;
  }

  const size_t written = std::fwrite(data, 1, length, g_state.active_log);
  std::fflush(g_state.active_log);
  return written == length;
}

std::vector<ListedFile> list_log_files_locked() {
  std::vector<ListedFile> files;
  DIR *dir = opendir(logs_dir_fs_path().c_str());
  if (dir == nullptr) {
    return files;
  }

  while (dirent *entry = readdir(dir)) {
    if (!starts_with(entry->d_name, kLogPrefix) || !ends_with(entry->d_name, kLogExtension)) {
      continue;
    }

    const std::string fs_path = logs_dir_fs_path() + "/" + entry->d_name;
    size_t size = 0;
    if (!file_info(fs_path, &size)) {
      continue;
    }

    ListedFile file;
    file.name = entry->d_name;
    file.web_path = std::string(kLogsDirWeb) + "/" + entry->d_name;
    file.size = size;
    file.index = extract_log_index(file.name);
    file.active = (file.web_path == g_state.active_log_web_path);
    files.push_back(file);
  }

  closedir(dir);

  std::sort(files.begin(), files.end(), [](const ListedFile &lhs, const ListedFile &rhs) {
    return lhs.index > rhs.index;
  });

  return files;
}

esp_err_t mount_littlefs() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kMountPoint;
  conf.partition_label = kPartitionLabel;
  conf.format_if_mount_failed = true;
  conf.grow_on_mount = true;

  const esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "LittleFS mount failed: %s", esp_err_to_name(err));
    return err;
  }

  size_t total_bytes = 0;
  size_t used_bytes = 0;
  if (esp_littlefs_info(kPartitionLabel, &total_bytes, &used_bytes) == ESP_OK) {
    ESP_LOGI(kTag, "LittleFS mounted: total=%u used=%u", static_cast<unsigned int>(total_bytes),
             static_cast<unsigned int>(used_bytes));
  }

  return ESP_OK;
}

}  // namespace fslogger
