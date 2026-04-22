//-*-C++-*-

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <openjpeg.h>

#include <parse/utils/jpeg/jpeg_utils.h>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

namespace pdflib {
namespace jpx {

class decoded_jpx_result {
public:
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  int components = 0;
  jpeg::ColorSpace color_space = jpeg::ColorSpace::Unknown;

  bool empty() const { return pixels.empty(); }
};

namespace detail {

struct memory_stream_context {
  uint8_t const* data = nullptr;
  OPJ_SIZE_T size = 0;
  OPJ_SIZE_T offset = 0;
};

inline OPJ_SIZE_T stream_read(void* buffer, OPJ_SIZE_T bytes, void* user_data)
{
  auto* ctx = reinterpret_cast<memory_stream_context*>(user_data);
  if(!ctx || !buffer || ctx->offset >= ctx->size) {
    return static_cast<OPJ_SIZE_T>(-1);
  }

  const OPJ_SIZE_T remaining = ctx->size - ctx->offset;
  const OPJ_SIZE_T to_copy = std::min(bytes, remaining);
  std::memcpy(buffer, ctx->data + ctx->offset, to_copy);
  ctx->offset += to_copy;
  return to_copy;
}

inline OPJ_OFF_T stream_skip(OPJ_OFF_T bytes, void* user_data)
{
  auto* ctx = reinterpret_cast<memory_stream_context*>(user_data);
  if(!ctx || bytes < 0) {
    return static_cast<OPJ_OFF_T>(-1);
  }

  const auto remaining = static_cast<OPJ_OFF_T>(ctx->size - ctx->offset);
  const auto to_skip = std::min(bytes, remaining);
  ctx->offset += static_cast<OPJ_SIZE_T>(to_skip);
  return to_skip;
}

inline OPJ_BOOL stream_seek(OPJ_OFF_T bytes, void* user_data)
{
  auto* ctx = reinterpret_cast<memory_stream_context*>(user_data);
  if(!ctx || bytes < 0 || static_cast<OPJ_SIZE_T>(bytes) > ctx->size) {
    return OPJ_FALSE;
  }

  ctx->offset = static_cast<OPJ_SIZE_T>(bytes);
  return OPJ_TRUE;
}

inline void stream_free(void*) {}

inline bool has_jp2_signature(uint8_t const* data, std::size_t size)
{
  static constexpr uint8_t kJP2Header[] = {
    0x00, 0x00, 0x00, 0x0c, 0x6a, 0x50,
    0x20, 0x20, 0x0d, 0x0a, 0x87, 0x0a
  };
  return data && size >= sizeof(kJP2Header)
      && std::memcmp(data, kJP2Header, sizeof(kJP2Header)) == 0;
}

inline void ignore_callback(const char*, void*) {}

inline uint8_t clamp_component_to_u8(int value, int precision, bool is_signed)
{
  if(precision <= 0) {
    return 0;
  }

  if(is_signed) {
    value += 1 << std::max(precision - 1, 0);
  }

  const int max_value = (precision >= 31) ? 0x7fffffff : ((1 << precision) - 1);
  value = std::clamp(value, 0, max_value);

  if(precision == 8) {
    return static_cast<uint8_t>(value);
  }

  if(precision > 8) {
    const int shift = precision - 8;
    value = (value >> shift) + ((value >> std::max(shift - 1, 0)) & (shift > 0 ? 1 : 0));
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
  }

  const int scaled = (value * 255 + max_value / 2) / std::max(max_value, 1);
  return static_cast<uint8_t>(std::clamp(scaled, 0, 255));
}

} // namespace detail

inline decoded_jpx_result decode_jpx_to_raw_pixels(uint8_t const* data,
                                                   std::size_t size)
{
  decoded_jpx_result result;

  if(!data || size == 0) {
    LOG_S(WARNING) << "decode_jpx_to_raw_pixels: empty input";
    return result;
  }

  detail::memory_stream_context ctx{data, static_cast<OPJ_SIZE_T>(size), 0};
  opj_stream_t* stream = opj_stream_create(1u << 16, OPJ_TRUE);
  if(!stream) {
    LOG_S(WARNING) << "decode_jpx_to_raw_pixels: failed to create stream";
    return result;
  }

  opj_stream_set_user_data(stream, &ctx, detail::stream_free);
  opj_stream_set_user_data_length(stream, ctx.size);
  opj_stream_set_read_function(stream, detail::stream_read);
  opj_stream_set_skip_function(stream, detail::stream_skip);
  opj_stream_set_seek_function(stream, detail::stream_seek);

  opj_dparameters_t params{};
  opj_set_default_decoder_parameters(&params);

  const auto codec_format =
      detail::has_jp2_signature(data, size) ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K;
  opj_codec_t* codec = opj_create_decompress(codec_format);
  if(!codec) {
    LOG_S(WARNING) << "decode_jpx_to_raw_pixels: failed to create codec";
    opj_stream_destroy(stream);
    return result;
  }

  opj_set_info_handler(codec, detail::ignore_callback, nullptr);
  opj_set_warning_handler(codec, detail::ignore_callback, nullptr);
  opj_set_error_handler(codec, detail::ignore_callback, nullptr);

  if(!opj_setup_decoder(codec, &params)) {
    LOG_S(WARNING) << "decode_jpx_to_raw_pixels: opj_setup_decoder failed";
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return result;
  }

  opj_image_t* image = nullptr;
  if(!opj_read_header(stream, codec, &image) ||
     !image ||
     !opj_decode(codec, stream, image) ||
     !opj_end_decompress(codec, stream)) {
    LOG_S(WARNING) << "decode_jpx_to_raw_pixels: OpenJPEG decode failed";
    if(image) {
      opj_image_destroy(image);
    }
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return result;
  }

  opj_stream_destroy(stream);
  opj_destroy_codec(codec);

  if(image->numcomps <= 0) {
    LOG_S(WARNING) << "decode_jpx_to_raw_pixels: no image components";
    opj_image_destroy(image);
    return result;
  }

  const int width = static_cast<int>(image->comps[0].w);
  const int height = static_cast<int>(image->comps[0].h);
  const int numcomps = static_cast<int>(image->numcomps);

  if(width <= 0 || height <= 0) {
    LOG_S(WARNING) << "decode_jpx_to_raw_pixels: invalid dimensions "
                   << width << "x" << height;
    opj_image_destroy(image);
    return result;
  }

  for(int c = 0; c < numcomps; ++c) {
    if(static_cast<int>(image->comps[c].w) != width ||
       static_cast<int>(image->comps[c].h) != height) {
      LOG_S(WARNING) << "decode_jpx_to_raw_pixels: unsupported subsampled component layout "
                     << "component=" << c
                     << " size=" << image->comps[c].w << "x" << image->comps[c].h
                     << " expected=" << width << "x" << height;
      opj_image_destroy(image);
      return result;
    }
  }

  result.width = width;
  result.height = height;
  result.components = numcomps;
  result.color_space = (numcomps == 1) ? jpeg::ColorSpace::Gray
                     : (numcomps == 3) ? jpeg::ColorSpace::RGB
                     : (numcomps == 4) ? jpeg::ColorSpace::CMYK
                                       : jpeg::ColorSpace::Unknown;

  const auto pixel_count = static_cast<std::size_t>(width) * height;
  result.pixels.resize(pixel_count * static_cast<std::size_t>(numcomps));

  for(std::size_t i = 0; i < pixel_count; ++i) {
    for(int c = 0; c < numcomps; ++c) {
      const auto& comp = image->comps[c];
      result.pixels[i * static_cast<std::size_t>(numcomps) + static_cast<std::size_t>(c)] =
          detail::clamp_component_to_u8(comp.data[i], comp.prec, comp.sgnd != 0);
    }
  }

  LOG_S(INFO) << "decode_jpx_to_raw_pixels: decoded "
              << width << "x" << height
              << " components=" << numcomps
              << " color_space=" << jpeg::color_space_name(result.color_space)
              << " input_size=" << size;

  opj_image_destroy(image);
  return result;
}

} // namespace jpx
} // namespace pdflib
