#include "logreader.h"

#include <algorithm>
#include <utility>

#include "common/util.h"
#include "decompress.h"
#include "filereader.h"
#include "util.h"

constexpr std::string_view BZ2_MAGIC = "BZh9";
constexpr std::string_view ZST_MAGIC = "\x28\xB5\x2F\xFD";

static std::string decompress(const std::string& url, std::string data, std::atomic<bool>* abort) {
  std::string_view header(data.data(), std::min(data.size(), size_t(4)));
  if (url.find(".bz2") != std::string::npos || header == BZ2_MAGIC) {
    return decompressBZ2(data, abort);
  } else if (url.find(".zst") != std::string::npos || header == ZST_MAGIC) {
    return decompressZST(data, abort);
  }
  return data;
}

bool LogReader::load(const std::string& url, bool low_memory, std::atomic<bool>* abort, bool local_cache, int chunk_size, int retries) {
  std::string data = FileReader(local_cache, chunk_size, retries).read(url, abort);
  if (data.empty()) return false;

  data = decompress(url, std::move(data), abort);
  if (data.empty()) return false;

  bool success = load(data.data(), data.size(), low_memory, abort);

  if (filters_.empty() || !low_memory) {
    raw_log_data_ = std::move(data);
  }
  return success;
}

kj::ArrayPtr<const capnp::word> LogReader::copyToBuffer(kj::ArrayPtr<const capnp::word> data) {
  size_t bytes = data.size() * sizeof(capnp::word);
  void* buf = buffer_.allocate(bytes);
  memcpy(buf, data.begin(), bytes);
  return kj::arrayPtr(reinterpret_cast<const capnp::word*>(buf), data.size());
}

void LogReader::addFrameEvents(cereal::Event::Which which, uint64_t mono_time, kj::ArrayPtr<const capnp::word> data) {
  if (which != cereal::Event::ROAD_ENCODE_IDX &&
      which != cereal::Event::DRIVER_ENCODE_IDX &&
      which != cereal::Event::WIDE_ROAD_ENCODE_IDX) return;

  capnp::FlatArrayMessageReader reader(data);
  auto idx = capnp::AnyStruct::Reader(reader.getRoot<cereal::Event>()).getPointerSection()[0].getAs<cereal::EncodeIndex>();
  if (idx.getType() == cereal::EncodeIndex::Type::FULL_H_E_V_C) {
    uint64_t sof = idx.getTimestampSof();
    events.emplace_back(which, sof ? sof : mono_time, data, idx.getSegmentNum());
  }
}

bool LogReader::load(const char *data, size_t size, bool low_memory, std::atomic<bool> *abort) {
  try {
    events.reserve(65000);
    kj::ArrayPtr<const capnp::word> words(reinterpret_cast<const capnp::word*>(data), size / sizeof(capnp::word));

    while (words.size() > 0 && !(abort && *abort)) {
      capnp::FlatArrayMessageReader reader(words);
      auto event = reader.getRoot<cereal::Event>();
      const auto which = event.which();
      auto event_data = kj::arrayPtr(words.begin(), reader.getEnd());
      words = kj::arrayPtr(reader.getEnd(), words.end());

      if (which == cereal::Event::Which::SELFDRIVE_STATE) {
        requires_migration = false;
      }

      // Apply filter
      if (!filters_.empty() && (which >= filters_.size() || !filters_[which])) continue;

      // In low memory mode with filters, copy filtered events to a separate buffer
      if (!filters_.empty() && low_memory) {
        event_data = copyToBuffer(event_data);
      }

      events.emplace_back(which, event.getLogMonoTime(), event_data);
      addFrameEvents(which, event.getLogMonoTime(), event_data);
    }
  } catch (const kj::Exception &e) {
    rWarning("Failed to parse log : %s.\nRetrieved %zu events from corrupt log", e.getDescription().cStr(), events.size());
  }

  if (requires_migration) {
    migrateOldEvents();
  }

  if (!events.empty() && !(abort && *abort)) {
    events.shrink_to_fit();
    std::sort(events.begin(), events.end());
    return true;
  }
  return false;
}

void LogReader::migrateOldEvents() {
  size_t events_size = events.size();
  for (size_t i = 0; i < events_size; ++i) {
    auto& event = events[i];
    if (event.which != cereal::Event::CONTROLS_STATE) continue;

    capnp::FlatArrayMessageReader reader(event.data);
    auto old_evt = reader.getRoot<cereal::Event>();
    auto old_state = old_evt.getControlsState();

    MessageBuilder msg;
    auto new_evt = msg.initEvent(old_evt.getValid());
    new_evt.setLogMonoTime(old_evt.getLogMonoTime());
    auto new_state = new_evt.initSelfdriveState();

    new_state.setActive(old_state.getActiveDEPRECATED());
    new_state.setAlertSize(old_state.getAlertSizeDEPRECATED());
    new_state.setAlertSound(old_state.getAlertSound2DEPRECATED());
    new_state.setAlertStatus(old_state.getAlertStatusDEPRECATED());
    new_state.setAlertText1(old_state.getAlertText1DEPRECATED());
    new_state.setAlertText2(old_state.getAlertText2DEPRECATED());
    new_state.setAlertType(old_state.getAlertTypeDEPRECATED());
    new_state.setEnabled(old_state.getEnabledDEPRECATED());
    new_state.setEngageable(old_state.getEngageableDEPRECATED());
    new_state.setExperimentalMode(old_state.getExperimentalModeDEPRECATED());
    new_state.setPersonality(old_state.getPersonalityDEPRECATED());
    new_state.setState(old_state.getStateDEPRECATED());

    auto serialized_size = msg.getSerializedSize();
    auto buf = buffer_.allocate(serialized_size);
    msg.serializeToBuffer(reinterpret_cast<unsigned char*>(buf), serialized_size);

    auto event_data = kj::arrayPtr(reinterpret_cast<const capnp::word*>(buf), serialized_size / sizeof(capnp::word));
    events.emplace_back(new_evt.which(), new_evt.getLogMonoTime(), event_data);
  }
}
