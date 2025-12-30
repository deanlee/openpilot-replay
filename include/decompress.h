#pragma once

#include <atomic>
#include <string>

std::string decompressBZ2(const std::string &in, std::atomic<bool> *abort = nullptr);
std::string decompressBZ2(const std::byte *in, size_t in_size, std::atomic<bool> *abort = nullptr);
std::string decompressZST(const std::string &in, std::atomic<bool> *abort = nullptr);
std::string decompressZST(const std::byte *in, size_t in_size, std::atomic<bool> *abort = nullptr);

