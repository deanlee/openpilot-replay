#include "http.h"

#include <curl/curl.h>

#include <algorithm>
#include <fstream>
#include <numeric>

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

// RAII wrappers for CURL handles
struct CurlEasy {
  CURL* handle = curl_easy_init();
  ~CurlEasy() { if (handle) curl_easy_cleanup(handle); }
  operator CURL*() const { return handle; }
  CurlEasy() = default;
  CurlEasy(CurlEasy&& o) noexcept : handle(std::exchange(o.handle, nullptr)) {}
  CurlEasy& operator=(CurlEasy&&) = delete;
  CurlEasy(const CurlEasy&) = delete;
  CurlEasy& operator=(const CurlEasy&) = delete;
};

struct CurlMulti {
  CURLM* handle = curl_multi_init();
  ~CurlMulti() { if (handle) curl_multi_cleanup(handle); }
  operator CURLM*() const { return handle; }
  CurlMulti() = default;
  CurlMulti(const CurlMulti&) = delete;
  CurlMulti& operator=(const CurlMulti&) = delete;
};

void runMulti(const CurlMulti& cm, std::atomic<bool>* abort, int wait_ms = 500) {
  int still_running = 1;
  while (still_running > 0 && !(abort && *abort)) {
    if (curl_multi_perform(cm, &still_running) != CURLM_OK) break;
    if (still_running > 0) {
      curl_multi_wait(cm, nullptr, 0, wait_ms, nullptr);
    }
  }
}

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
struct PartWriter {
  T* buf;
  size_t offset;
  size_t end;
  size_t written = 0;
  std::string range_header;

  size_t write(char* data, size_t size, size_t count) {
    size_t bytes = size * count;
    if (offset + bytes > end) return 0;

    if constexpr (std::is_same_v<T, std::string>) {
      memcpy(buf->data() + offset, data, bytes);
    } else if constexpr (std::is_same_v<T, std::ofstream>) {
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
  return static_cast<PartWriter<T>*>(userp)->write(data, size, count);
}

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
  CurlEasy curl;
  if (!curl.handle) return 0;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  if (abort) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                     +[](void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                       return *static_cast<std::atomic<bool>*>(p) ? 1 : 0;
                     });
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, abort);
  }

  if (curl_easy_perform(curl) != CURLE_OK) return 0;

  double content_length = -1;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
  return content_length > 0 ? static_cast<size_t>(content_length) : 0;
}

std::string getUrlWithoutQuery(const std::string& url) {
  size_t idx = url.find("?");
  return (idx == std::string::npos ? url : url.substr(0, idx));
}

template <class T>
bool httpDownload(const std::string& url, T& buf, size_t chunk_size, size_t content_length, std::atomic<bool>* abort) {
  g_stats.add(content_length);

  size_t threshold = (chunk_size > 0) ? chunk_size : DEFAULT_CHUNK_SIZE;
  int parts = std::clamp(static_cast<int>((content_length + threshold - 1) / threshold), 1, MAX_DOWNLOAD_PARTS);
  size_t part_size = content_length / parts;

  CurlMulti cm;
  std::vector<CurlEasy> handles(parts);
  std::vector<PartWriter<T>> writers;
  writers.reserve(parts);

  for (int i = 0; i < parts; ++i) {
    size_t start = i * part_size;
    size_t end = (i == parts - 1) ? content_length : (i + 1) * part_size;
    writers.push_back({&buf, start, end, 0, util::string_format("%zu-%zu", start, end - 1)});

    CURL* eh = handles[i];
    curl_easy_setopt(eh, CURLOPT_URL, url.c_str());
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb<T>);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, &writers.back());
    curl_easy_setopt(eh, CURLOPT_RANGE, writers.back().range_header.c_str());
    curl_easy_setopt(eh, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(eh, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
    curl_multi_add_handle(cm, eh);
  }

  runMulti(cm, abort);

  // Verify results
  int success_count = 0;
  int msgs_left = -1;
  CURLMsg* msg;
  while ((msg = curl_multi_info_read(cm, &msgs_left))) {
    if (msg->msg != CURLMSG_DONE) continue;

    long code = 0;
    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &code);

    if (msg->data.result == CURLE_OK && (code == 200 || code == 206)) {
      success_count++;
    } else {
      rWarning("Download failed: %s (HTTP %ld)", curl_easy_strerror(msg->data.result), code);
    }
  }

  for (auto& eh : handles) {
    curl_multi_remove_handle(cm, eh);
  }

  uint64_t total_written = std::accumulate(writers.begin(), writers.end(), 0ULL,
                           [](uint64_t sum, const auto& w) { return sum + w.written; });
  bool success = (success_count == parts) && !(abort && *abort);
  g_stats.update(0, success, true);
  g_stats.remove(content_length, total_written);

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
