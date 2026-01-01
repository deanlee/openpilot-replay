#pragma once

#include <set>
#include <string>

#define DEMO_ROUTE "a2a0ccea32023010|2023-07-27--13-01-19"

constexpr int MIN_SEGMENTS_CACHE = 5;

enum CameraType {
  RoadCam = 0,
  DriverCam,
  WideRoadCam
};

const CameraType ALL_CAMERAS[] = {RoadCam, DriverCam, WideRoadCam};
const int MAX_CAMERAS = std::size(ALL_CAMERAS);

enum REPLAY_FLAGS {
  REPLAY_FLAG_NONE = 0x0000,
  REPLAY_FLAG_DCAM = 0x0002,
  REPLAY_FLAG_ECAM = 0x0004,
  REPLAY_FLAG_NO_LOOP = 0x0010,
  REPLAY_FLAG_NO_FILE_CACHE = 0x0020,
  REPLAY_FLAG_QCAMERA = 0x0040,
  REPLAY_FLAG_NO_HW_DECODER = 0x0100,
  REPLAY_FLAG_NO_VIPC = 0x0400,
  REPLAY_FLAG_ALL_SERVICES = 0x0800,
  REPLAY_FLAG_LOW_MEMORY = 0x1000,
};

struct ReplayConfig {
  std::string route;
  std::set<std::string> allow;
  std::set<std::string> block;
  std::string data_dir;
  std::string prefix;
  uint32_t flags = REPLAY_FLAG_NONE;
  bool auto_source = false;
  int start_seconds = 0;
  int cache_segments = MIN_SEGMENTS_CACHE;
  float playback_speed = 1.0f;
};
