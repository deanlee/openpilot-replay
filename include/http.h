#pragma once
#include <atomic>
#include <functional>
#include <sstream>

typedef std::function<void(uint64_t cur, uint64_t total, bool success)> DownloadProgressHandler;
void installDownloadProgressHandler(DownloadProgressHandler);

size_t getRemoteFileSize(const std::string& url, std::atomic<bool>* abort = nullptr);
std::string getUrlWithoutQuery(const std::string& url);

std::string httpGet(const std::string& url, size_t chunk_size = 0, std::atomic<bool>* abort = nullptr);
bool httpDownload(const std::string& url, const std::string& file, size_t chunk_size = 0, std::atomic<bool>* abort = nullptr);
