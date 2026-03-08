#include "decompress.h"

#include <bzlib.h>
#include <zstd.h>

#include <cassert>

#include "util.h"

std::string decompressBZ2(const std::string& in, std::atomic<bool>* abort) {
  if (in.empty()) return {};

  bz_stream strm = {};
  int err = BZ2_bzDecompressInit(&strm, 0, 0);
  assert(err == BZ_OK);

  strm.next_in = const_cast<char*>(in.data());
  strm.avail_in = in.size();
  std::string out(in.size() * 5, '\0');

  while (err == BZ_OK && !(abort && *abort)) {
    strm.next_out = out.data() + strm.total_out_lo32;
    strm.avail_out = out.size() - strm.total_out_lo32;

    const char* prev = strm.next_out;
    err = BZ2_bzDecompress(&strm);

    if (err == BZ_OK && prev == strm.next_out) {
      rWarning("decompressBZ2 error: content is corrupt");
      break;
    }
    if (err == BZ_OK && strm.avail_out == 0) {
      out.resize(out.size() * 2);
    }
  }

  BZ2_bzDecompressEnd(&strm);

  if (err != BZ_STREAM_END || (abort && *abort)) return {};
  out.resize(strm.total_out_lo32);
  out.shrink_to_fit();
  return out;
}

std::string decompressZST(const std::string& in, std::atomic<bool>* abort) {
  if (in.empty()) return {};

  // Single-shot when content size is known
  size_t frame_size = ZSTD_getFrameContentSize(in.data(), in.size());
  if (frame_size != ZSTD_CONTENTSIZE_ERROR && frame_size != ZSTD_CONTENTSIZE_UNKNOWN) {
    std::string out(frame_size, '\0');
    size_t result = ZSTD_decompress(out.data(), frame_size, in.data(), in.size());
    if (ZSTD_isError(result)) {
      rWarning("decompressZST error: %s", ZSTD_getErrorName(result));
      return {};
    }
    out.resize(result);
    return out;
  }

  // Streaming fallback for unknown content size
  ZSTD_DCtx* dctx = ZSTD_createDCtx();
  assert(dctx != nullptr);

  ZSTD_inBuffer input = {in.data(), in.size(), 0};
  std::string out(in.size() * 5, '\0');
  size_t pos = 0;

  while (input.pos < input.size && !(abort && *abort)) {
    ZSTD_outBuffer output = {out.data() + pos, out.size() - pos, 0};
    size_t result = ZSTD_decompressStream(dctx, &output, &input);
    if (ZSTD_isError(result)) {
      rWarning("decompressZST error: %s", ZSTD_getErrorName(result));
      ZSTD_freeDCtx(dctx);
      return {};
    }
    pos += output.pos;
    if (input.pos < input.size && pos == out.size()) {
      out.resize(out.size() * 2);
    }
  }

  ZSTD_freeDCtx(dctx);
  if (abort && *abort) return {};

  out.resize(pos);
  out.shrink_to_fit();
  return out;
}
