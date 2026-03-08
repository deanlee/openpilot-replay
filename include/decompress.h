#pragma once

#include <atomic>
#include <string>

std::string decompressBZ2(const std::string &in, std::atomic<bool> *abort = nullptr);
std::string decompressZST(const std::string &in, std::atomic<bool> *abort = nullptr);
