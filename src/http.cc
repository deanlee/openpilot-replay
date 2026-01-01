#include "http.h"

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>
#include <utility>

#include "config.h"
#include "common/timing.h"
#include "common/util.h"
#include "util.h"

namespace {

struct CURLGlobalInitializer {
  CURLGlobalInitializer() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CURLGlobalInitializer() { curl_global_cleanup(); }
};

static CURLGlobalInitializer curl_initializer;

// Thread-safe Global Progress Tracker
struct DownloadStats {
  std::atomic<uint64_t> total_bytes{0};
  std::atomic<uint64_t> downloaded_bytes{0};
  std::atomic<double> prev_tm{0};
  DownloadProgressHandler handler = nullptr;

  void add(uint64_t size) { total_bytes += size; }

  void update(size_t delta, bool success = true, bool final_call = false) {
    uint64_t current = (downloaded_bytes += delta);
    uint64_t total = total_bytes.load(std::memory_order_relaxed);

    double now = millis_since_boot();
    double prev = prev_tm.load(std::memory_order_relaxed);

    // Throttle UI calls to 500ms
    if (final_call || (now - prev > 500)) {
      if (!final_call) {
        if (!prev_tm.compare_exchange_strong(prev, now)) return;
      }
      if (handler) handler(current, total, success);
    }
  }

  void remove(uint64_t file_total, uint64_t file_downloaded) {
    total_bytes -= file_total;
    downloaded_bytes -= file_downloaded;
  }
};

static DownloadStats g_stats;

template <class T>
struct MultiPartWriter {
  T* buf;
  size_t offset;
  size_t end;
  size_t written = 0;

  size_t write(char* data, size_t size, size_t count) {
    size_t bytes = size * count;
    if ((offset + bytes) > end) return 0;

    if constexpr (std::is_same<T, std::string>::value) {
      memcpy(buf->data() + offset, data, bytes);
    } else if constexpr (std::is_same<T, std::ofstream>::value) {
      buf->seekp(offset);
      buf->write(data, bytes);
    }

    offset += bytes;
    written += bytes;
    g_stats.update(bytes);
    return bytes;
  }
};

template <class T>
size_t write_cb(char* data, size_t size, size_t count, void* userp) {
  auto w = (MultiPartWriter<T>*)userp;
  return w->write(data, size, count);
}

size_t dumy_write_cb(char* data, size_t size, size_t count, void* userp) { return size * count; }

}  // namespace

void installDownloadProgressHandler(DownloadProgressHandler handler) {
  g_stats.handler = handler;
}

std::string formattedDataSize(size_t size) {
  if (size < 1024) {
    return std::to_string(size) + " B";
  } else if (size < 1024 * 1024) {
    return util::string_format("%.2f KB", (float)size / 1024);
  } else {
    return util::string_format("%.2f MB", (float)size / (1024 * 1024));
  }
}

size_t getRemoteFileSize(const std::string& url, std::atomic<bool>* abort) {
  CURL* curl = curl_easy_init();
  if (!curl) return -1;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dumy_write_cb);
  curl_easy_setopt(curl, CURLOPT_HEADER, 1);
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

  CURLM* cm = curl_multi_init();
  curl_multi_add_handle(cm, curl);
  int still_running = 1;
  while (still_running > 0 && !(abort && *abort)) {
    CURLMcode mc = curl_multi_perform(cm, &still_running);
    if (mc != CURLM_OK) break;
    if (still_running > 0) {
      curl_multi_wait(cm, nullptr, 0, 1000, nullptr);
    }
  }

  double content_length = -1;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
  curl_multi_remove_handle(cm, curl);
  curl_easy_cleanup(curl);
  curl_multi_cleanup(cm);
  return content_length > 0 ? (size_t)content_length : 0;
}

std::string getUrlWithoutQuery(const std::string& url) {
  size_t idx = url.find("?");
  return (idx == std::string::npos ? url : url.substr(0, idx));
}

template <class T>
bool httpDownload(const std::string& url, T& buf, size_t chunk_size, size_t content_length, std::atomic<bool>* abort) {
  g_stats.add(content_length);

  int parts = 1;
  if (chunk_size > 0 && content_length > 10 * 1024 * 1024) {
    parts = std::nearbyint(content_length / (float)chunk_size);
    parts = std::clamp(parts, 1, MAX_DOWNLOAD_PARTS);
  }

  CURLM* cm = curl_multi_init();
  std::map<CURL*, MultiPartWriter<T>> writers;
  const int part_size = content_length / parts;
  for (int i = 0; i < parts; ++i) {
    CURL* eh = curl_easy_init();
    writers[eh] = {
        .buf = &buf,
        .offset = (size_t)(i * part_size),
        .end = i == parts - 1 ? content_length : (i + 1) * part_size,
        .written = 0,
    };
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb<T>);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, (void*)(&writers[eh]));
    curl_easy_setopt(eh, CURLOPT_URL, url.c_str());
    curl_easy_setopt(eh, CURLOPT_RANGE, util::string_format("%d-%d", writers[eh].offset, writers[eh].end - 1).c_str());
    curl_easy_setopt(eh, CURLOPT_HTTPGET, 1);
    curl_easy_setopt(eh, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1);

    curl_multi_add_handle(cm, eh);
  }

  // Event loop
  int still_running = 1;
  while (still_running > 0 && !(abort && *abort)) {
    curl_multi_perform(cm, &still_running);
    if (still_running > 0) {
      curl_multi_wait(cm, nullptr, 0, 500, nullptr);
    }
  }

  // Verification
  int success_count = 0;
  CURLMsg* msg;
  int msgs_left = -1;
  while ((msg = curl_multi_info_read(cm, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      if (msg->data.result == CURLE_OK) {
        long res_status = 0;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &res_status);
        if (res_status == 206) {
          success_count++;
        } else {
          rWarning("Download failed: http error code: %d", res_status);
        }
      } else {
        rWarning("Download failed: connection failure: %d", msg->data.result);
      }
    }
  }

  for (const auto& [e, w] : writers) {
    curl_multi_remove_handle(cm, e);
    curl_easy_cleanup(e);
  }
  curl_multi_cleanup(cm);

  uint64_t total_file_written = 0;
  for (const auto& [_, w] : writers) total_file_written += w.written;
  bool success = (success_count == parts) && !(abort && *abort);
  g_stats.update(0, success, true); // Force final UI update
  if (!success) g_stats.remove(content_length, total_file_written);

  return success;
}

std::string httpGet(const std::string& url, size_t chunk_size, std::atomic<bool>* abort) {
  size_t size = getRemoteFileSize(url, abort);
  if (size == 0) return {};

  std::string result(size, '\0');
  return httpDownload(url, result, chunk_size, size, abort) ? result : "";
}

bool httpDownload(const std::string& url, const std::string& file, size_t chunk_size, std::atomic<bool>* abort) {
  size_t size = getRemoteFileSize(url, abort);
  if (size == 0) return false;

  std::ofstream of(file, std::ios::binary | std::ios::out);
  of.seekp(size - 1).write("\0", 1);
  return httpDownload(url, of, chunk_size, size, abort);
}
