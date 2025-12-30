#include "decompress.h"

#include <bzlib.h>
#include <zstd.h>

#include <cassert>
#include <string>

#include "util.h"

std::string decompressBZ2(const std::string& in, std::atomic<bool>* abort) {
  return decompressBZ2((std::byte*)in.data(), in.size(), abort);
}

std::string decompressBZ2(const std::byte* in, size_t in_size, std::atomic<bool>* abort) {
  if (in_size == 0) return {};

  bz_stream strm = {};
  int bzerror = BZ2_bzDecompressInit(&strm, 0, 0);
  assert(bzerror == BZ_OK);

  strm.next_in = (char*)in;
  strm.avail_in = in_size;
  std::string out(in_size * 5, '\0');
  do {
    strm.next_out = (char*)(&out[strm.total_out_lo32]);
    strm.avail_out = out.size() - strm.total_out_lo32;

    const char* prev_write_pos = strm.next_out;
    bzerror = BZ2_bzDecompress(&strm);
    if (bzerror == BZ_OK && prev_write_pos == strm.next_out) {
      // content is corrupt
      bzerror = BZ_STREAM_END;
      rWarning("decompressBZ2 error: content is corrupt");
      break;
    }

    if (bzerror == BZ_OK && strm.avail_in > 0 && strm.avail_out == 0) {
      out.resize(out.size() * 2);
    }
  } while (bzerror == BZ_OK && !(abort && *abort));

  BZ2_bzDecompressEnd(&strm);
  if (bzerror == BZ_STREAM_END && !(abort && *abort)) {
    out.resize(strm.total_out_lo32);
    out.shrink_to_fit();
    return out;
  }
  return {};
}

std::string decompressZST(const std::string& in, std::atomic<bool>* abort) {
  return decompressZST((std::byte*)in.data(), in.size(), abort);
}

std::string decompressZST(const std::byte* in, size_t in_size, std::atomic<bool>* abort) {
  ZSTD_DCtx* dctx = ZSTD_createDCtx();
  assert(dctx != nullptr);

  // Initialize input and output buffers
  ZSTD_inBuffer input = {in, in_size, 0};

  // Estimate and reserve memory for decompressed data
  size_t estimatedDecompressedSize = ZSTD_getFrameContentSize(in, in_size);
  if (estimatedDecompressedSize == ZSTD_CONTENTSIZE_ERROR || estimatedDecompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
    estimatedDecompressedSize = in_size * 2;  // Use a fallback size
  }

  std::string decompressedData;
  decompressedData.reserve(estimatedDecompressedSize);

  const size_t bufferSize = ZSTD_DStreamOutSize();  // Recommended output buffer size
  std::string outputBuffer(bufferSize, '\0');

  while (input.pos < input.size && !(abort && *abort)) {
    ZSTD_outBuffer output = {outputBuffer.data(), bufferSize, 0};

    size_t result = ZSTD_decompressStream(dctx, &output, &input);
    if (ZSTD_isError(result)) {
      rWarning("decompressZST error: content is corrupt");
      break;
    }

    decompressedData.append(outputBuffer.data(), output.pos);
  }

  ZSTD_freeDCtx(dctx);
  if (!(abort && *abort)) {
    decompressedData.shrink_to_fit();
    return decompressedData;
  }
  return {};
}
