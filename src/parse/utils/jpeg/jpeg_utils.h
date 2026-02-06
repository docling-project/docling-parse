//-*-C++-*-

#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <csetjmp>

#include <jpeglib.h>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

namespace pdflib {
namespace jpeg {

enum class ColorSpace { Gray, RGB, CMYK, Unknown };

inline char const* color_space_name(ColorSpace cs)
{
  switch(cs) {
    case ColorSpace::Gray:    return "Gray";
    case ColorSpace::RGB:     return "RGB";
    case ColorSpace::CMYK:    return "CMYK";
    case ColorSpace::Unknown: return "Unknown";
  }
  return "?";
}

inline ColorSpace to_color_space(std::string const& cs)
{
  if(cs == "/DeviceGray") return ColorSpace::Gray;
  if(cs == "/DeviceRGB")  return ColorSpace::RGB;
  if(cs == "/DeviceCMYK") return ColorSpace::CMYK;
  return ColorSpace::Unknown;
}

class jpeg_parameters {
public:
  int width = 0;
  int height = 0;
  int bits_per_component = 8;
  ColorSpace color_space = ColorSpace::Unknown;
  std::vector<double> decode; // length 2*ncomp; empty if absent
  bool has_decode = false;
  bool image_mask = false;
};

// ---------------------------------------------------------------------------
// Custom libjpeg error handler that longjmp's instead of calling exit()
// ---------------------------------------------------------------------------
struct jpeg_error_longjmp : public jpeg_error_mgr
{
  std::jmp_buf jmp;
};

inline void jpeg_error_exit_longjmp(j_common_ptr cinfo)
{
  auto* myerr = reinterpret_cast<jpeg_error_longjmp*>(cinfo->err);
  char buf[JMSG_LENGTH_MAX];
  (*cinfo->err->format_message)(cinfo, buf);
  LOG_S(WARNING) << "libjpeg error: " << buf;
  std::longjmp(myerr->jmp, 1);
}

// ---------------------------------------------------------------------------
// Validate that data begins with the JPEG SOI marker (0xFF 0xD8)
// ---------------------------------------------------------------------------
inline bool is_jpeg_data(unsigned char const* data, std::size_t size)
{
  return size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

// ---------------------------------------------------------------------------
// apply_decode_component
// ---------------------------------------------------------------------------
// Implements the PDF /Decode linear mapping (PDF spec 8.9.5.2):
//
//     output = Dmin + (Dmax - Dmin) * (sample / 255)
//
// An identity pair [0 1] is a no-op.  A reversed pair [1 0] inverts the
// component.
// ---------------------------------------------------------------------------
inline unsigned char apply_decode_component(unsigned char v, double dmin, double dmax)
{
  double t = static_cast<double>(v) / 255.0;
  double u = dmin + (dmax - dmin) * t;
  return static_cast<unsigned char>(std::clamp(
      static_cast<int>(std::lround(u * 255.0)), 0, 255));
}

// ---------------------------------------------------------------------------
// write_corrected_jpeg_from_memory
// ---------------------------------------------------------------------------
// Decodes a JPEG from a raw memory buffer (as stored in a PDF stream),
// applies the PDF /Decode mapping, and re-encodes the result as JPEG on
// disk.  The output colour space matches the input (CMYK stays CMYK).
//
// The PDF /Decode array (§8.9.5.2) linearly maps each decompressed sample
// through a [Dmin, Dmax] pair per component.  An identity pair [0 1] is a
// no-op; a reversed pair [1 0] inverts the component.
//
// For CMYK images the /Decode array is the authoritative mechanism by
// which the PDF signals channel conventions.  A /Decode of
// [1 0 1 0 1 0 1 0] means all four channels must be inverted (the Adobe
// inverted convention where 0 = full ink).
//
// Processing order
// ~~~~~~~~~~~~~~~~
//   a. Decompress JPEG via libjpeg  (handles YCbCr / YCCK internally)
//   b. Apply /Decode mapping to every component (all colour spaces)
//   c. Re-encode as JPEG in the original colour space
// ---------------------------------------------------------------------------
inline bool write_corrected_jpeg_from_memory(
    unsigned char const* data, std::size_t size,
    jpeg_parameters const& params,
    std::filesystem::path const& path)
{
  LOG_S(INFO) << __FUNCTION__
              << ": input_size=" << size
              << " requested_cs=" << color_space_name(params.color_space)
              << " has_decode=" << params.has_decode
              << " decode_len=" << params.decode.size()
              << " image_mask=" << params.image_mask
              << " path=" << path.string();

  if(params.has_decode && !params.decode.empty())
  {
    std::string dec_str;
    for(std::size_t i = 0; i < params.decode.size(); ++i)
    {
      if(i > 0) dec_str += " ";
      dec_str += std::to_string(params.decode[i]);
    }
    LOG_S(INFO) << __FUNCTION__ << ": /Decode values = [" << dec_str << "]";
  }

  if((not data) or (size == 0))
    {
      LOG_S(INFO) << __FUNCTION__ << ": data is null or size is zero";
      return false;
    }

  if(!is_jpeg_data(data, size))
    {
      LOG_S(WARNING) << __FUNCTION__
                     << ": data does not start with JPEG SOI marker"
                     << " (starts with 0x" << std::hex
                     << static_cast<int>(data[0]) << " 0x"
                     << static_cast<int>(size > 1 ? data[1] : 0)
                     << std::dec << "), skipping";
      return false;
    }

  // --- Decompress --------------------------------------------------------
  LOG_S(INFO) << "starting the jpeg decompression ...";

  jpeg_decompress_struct dinfo{};
  jpeg_error_longjmp jerr{};

  dinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpeg_error_exit_longjmp;
  jpeg_create_decompress(&dinfo);

  if(setjmp(jerr.jmp))
  {
    jpeg_destroy_decompress(&dinfo);
    return false;
  }

  jpeg_mem_src(&dinfo, const_cast<unsigned char*>(data),
               static_cast<unsigned long>(size));

  if(JPEG_HEADER_OK != jpeg_read_header(&dinfo, TRUE))
  {
    LOG_S(WARNING) << __FUNCTION__ << ": jpeg_read_header failed";
    jpeg_destroy_decompress(&dinfo);
    return false;
  }

  LOG_S(INFO) << __FUNCTION__
              << ": JPEG header: jpeg_color_space=" << dinfo.jpeg_color_space
              << " num_components=" << dinfo.num_components
              << " image_width=" << dinfo.image_width
              << " image_height=" << dinfo.image_height;

  switch(params.color_space)
  {
    case ColorSpace::Gray: dinfo.out_color_space = JCS_GRAYSCALE; break;
    case ColorSpace::RGB:  dinfo.out_color_space = JCS_RGB;       break;
    case ColorSpace::CMYK: dinfo.out_color_space = JCS_CMYK;      break;
    default: break;
  }

  LOG_S(INFO) << __FUNCTION__
              << ": requesting out_color_space=" << dinfo.out_color_space;

  jpeg_start_decompress(&dinfo);

  // Save all decompressor state we need before destroying it
  const int         ncomp     = dinfo.output_components;
  const std::size_t w         = dinfo.output_width;
  const std::size_t h         = dinfo.output_height;
  const std::size_t stride    = w * static_cast<std::size_t>(ncomp);
  const bool        is_cmyk   = (dinfo.out_color_space == JCS_CMYK);

  LOG_S(INFO) << __FUNCTION__
              << ": decompressed: w=" << w << " h=" << h
              << " ncomp=" << ncomp << " stride=" << stride
              << " is_cmyk=" << is_cmyk
              << " out_color_space=" << dinfo.out_color_space
              << " jpeg_color_space=" << dinfo.jpeg_color_space;

  std::vector<unsigned char> image(h * stride);

  while(dinfo.output_scanline < dinfo.output_height)
  {
    unsigned char* row = &image[dinfo.output_scanline * stride];
    JSAMPROW rows[1] = { row };
    jpeg_read_scanlines(&dinfo, rows, 1);
  }

  jpeg_finish_decompress(&dinfo);
  jpeg_destroy_decompress(&dinfo);

  // Log a few sample pixels from the top-left corner (before /Decode)
  if(h > 0 && w > 0)
  {
    std::string sample;
    int npx = std::min(static_cast<int>(w), 3);
    for(int px = 0; px < npx; ++px)
    {
      sample += " px[" + std::to_string(px) + "]=(";
      for(int c = 0; c < ncomp; ++c)
      {
        if(c > 0) sample += ",";
        sample += std::to_string(static_cast<int>(image[px * ncomp + c]));
      }
      sample += ")";
    }
    LOG_S(INFO) << __FUNCTION__ << ": sample pixels BEFORE /Decode:" << sample;
  }

  // --- Apply /Decode mapping (all colour spaces) -------------------------
  // The /Decode array linearly maps each decompressed sample through
  // [Dmin, Dmax] per component.  Identity [0 1] is a no-op; reversed
  // [1 0] inverts.  For CMYK, /Decode [1 0 1 0 1 0 1 0] is how PDFs
  // signal the inverted Adobe channel convention.
  if(params.has_decode && !params.decode.empty() &&
     static_cast<int>(params.decode.size()) >= 2 * ncomp)
  {
    LOG_S(INFO) << __FUNCTION__ << ": applying /Decode mapping to " << ncomp << " components";

    for(std::size_t y = 0; y < h; ++y)
    {
      unsigned char* row = &image[y * stride];
      for(std::size_t x = 0; x < w; ++x)
      {
        for(int c = 0; c < ncomp; ++c)
        {
          double dmin = params.decode[2 * c + 0];
          double dmax = params.decode[2 * c + 1];
          row[x * ncomp + c] = apply_decode_component(
              row[x * ncomp + c], dmin, dmax);
        }
      }
    }

    // Log a few sample pixels after /Decode
    if(h > 0 && w > 0)
    {
      std::string sample;
      int npx = std::min(static_cast<int>(w), 3);
      for(int px = 0; px < npx; ++px)
      {
        sample += " px[" + std::to_string(px) + "]=(";
        for(int c = 0; c < ncomp; ++c)
        {
          if(c > 0) sample += ",";
          sample += std::to_string(static_cast<int>(image[px * ncomp + c]));
        }
        sample += ")";
      }
      LOG_S(INFO) << __FUNCTION__ << ": sample pixels AFTER /Decode:" << sample;
    }
  }
  else
  {
    LOG_S(INFO) << __FUNCTION__ << ": skipping /Decode"
                << " (has_decode=" << params.has_decode
                << " decode_empty=" << params.decode.empty()
                << " decode_size=" << params.decode.size()
                << " 2*ncomp=" << (2 * ncomp) << ")";
  }

  // --- Re-encode (preserving original colour space) ----------------------
  jpeg_compress_struct cinfo{};
  jpeg_error_longjmp cjerr{};
  cinfo.err = jpeg_std_error(&cjerr);
  cjerr.error_exit = jpeg_error_exit_longjmp;
  jpeg_create_compress(&cinfo);

  std::FILE* outfile = std::fopen(path.string().c_str(), "wb");
  if(!outfile)
  {
    LOG_S(ERROR) << __FUNCTION__ << ": failed to open output file: " << path.string();
    jpeg_destroy_compress(&cinfo);
    return false;
  }

  if(setjmp(cjerr.jmp))
  {
    std::fclose(outfile);
    jpeg_destroy_compress(&cinfo);
    return false;
  }

  jpeg_stdio_dest(&cinfo, outfile);

  cinfo.image_width      = static_cast<JDIMENSION>(w);
  cinfo.image_height     = static_cast<JDIMENSION>(h);
  cinfo.input_components = ncomp;

  if(is_cmyk)
    cinfo.in_color_space = JCS_CMYK;
  else
    cinfo.in_color_space = (ncomp == 1) ? JCS_GRAYSCALE : JCS_RGB;

  LOG_S(INFO) << __FUNCTION__
              << ": re-encoding: w=" << w << " h=" << h
              << " ncomp=" << ncomp
              << " in_color_space=" << cinfo.in_color_space
              << " is_cmyk=" << is_cmyk;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 90, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  for(std::size_t y = 0; y < h; ++y)
  {
    JSAMPROW row[1] = { const_cast<unsigned char*>(&image[y * stride]) };
    jpeg_write_scanlines(&cinfo, row, 1);
  }

  jpeg_finish_compress(&cinfo);
  std::fclose(outfile);
  jpeg_destroy_compress(&cinfo);

  LOG_S(INFO) << __FUNCTION__ << ": successfully wrote corrected JPEG to " << path.string();

  return true;
}

// ---------------------------------------------------------------------------
// write_corrected_jpeg_to_memory
// ---------------------------------------------------------------------------
// Same as write_corrected_jpeg_from_memory but writes to a memory buffer
// instead of a file.  Returns the corrected JPEG as a byte vector, or an
// empty vector on failure.
// ---------------------------------------------------------------------------
inline std::vector<unsigned char> write_corrected_jpeg_to_memory(
    unsigned char const* data, std::size_t size,
    jpeg_parameters const& params)
{
  if(not data or size == 0) { return {}; }

  if(!is_jpeg_data(data, size))
  {
    LOG_S(WARNING) << "write_corrected_jpeg_to_memory"
                   << ": data does not start with JPEG SOI marker, skipping";
    return {};
  }

  // --- Decompress --------------------------------------------------------
  jpeg_decompress_struct dinfo{};
  jpeg_error_longjmp jerr{};
  dinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpeg_error_exit_longjmp;
  jpeg_create_decompress(&dinfo);

  if(setjmp(jerr.jmp))
  {
    jpeg_destroy_decompress(&dinfo);
    return {};
  }

  jpeg_mem_src(&dinfo, const_cast<unsigned char*>(data),
               static_cast<unsigned long>(size));

  if(JPEG_HEADER_OK != jpeg_read_header(&dinfo, TRUE))
  {
    jpeg_destroy_decompress(&dinfo);
    return {};
  }

  switch(params.color_space)
  {
    case ColorSpace::Gray: { dinfo.out_color_space = JCS_GRAYSCALE; break; }
    case ColorSpace::RGB:  { dinfo.out_color_space = JCS_RGB;       break; }
    case ColorSpace::CMYK: { dinfo.out_color_space = JCS_CMYK;      break; }
    default: { break; }
  }

  jpeg_start_decompress(&dinfo);

  const int         ncomp  = dinfo.output_components;
  const std::size_t w      = dinfo.output_width;
  const std::size_t h      = dinfo.output_height;
  const std::size_t stride = w * static_cast<std::size_t>(ncomp);
  const bool        is_cmyk = (dinfo.out_color_space == JCS_CMYK);

  std::vector<unsigned char> image(h * stride);

  while(dinfo.output_scanline < dinfo.output_height)
  {
    unsigned char* row = &image[dinfo.output_scanline * stride];
    JSAMPROW rows[1] = { row };
    jpeg_read_scanlines(&dinfo, rows, 1);
  }

  jpeg_finish_decompress(&dinfo);
  jpeg_destroy_decompress(&dinfo);

  // --- Apply /Decode mapping ---------------------------------------------
  if(params.has_decode and not params.decode.empty() and
     static_cast<int>(params.decode.size()) >= 2 * ncomp)
  {
    for(std::size_t y = 0; y < h; ++y)
    {
      unsigned char* row = &image[y * stride];
      for(std::size_t x = 0; x < w; ++x)
      {
        for(int c = 0; c < ncomp; ++c)
        {
          double dmin = params.decode[2 * c + 0];
          double dmax = params.decode[2 * c + 1];
          row[x * ncomp + c] = apply_decode_component(
              row[x * ncomp + c], dmin, dmax);
        }
      }
    }
  }

  // --- Re-encode to memory -----------------------------------------------
  unsigned char* outbuf  = nullptr;
  unsigned long  outsize = 0;

  jpeg_compress_struct cinfo{};
  jpeg_error_longjmp cjerr{};
  cinfo.err = jpeg_std_error(&cjerr);
  cjerr.error_exit = jpeg_error_exit_longjmp;
  jpeg_create_compress(&cinfo);

  if(setjmp(cjerr.jmp))
  {
    jpeg_destroy_compress(&cinfo);
    if(outbuf) free(outbuf);
    return {};
  }

  jpeg_mem_dest(&cinfo, &outbuf, &outsize);

  cinfo.image_width      = static_cast<JDIMENSION>(w);
  cinfo.image_height     = static_cast<JDIMENSION>(h);
  cinfo.input_components = ncomp;

  if(is_cmyk)
  {
    cinfo.in_color_space = JCS_CMYK;
  }
  else
  {
    cinfo.in_color_space = (ncomp == 1) ? JCS_GRAYSCALE : JCS_RGB;
  }

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 90, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  for(std::size_t y = 0; y < h; ++y)
  {
    JSAMPROW row[1] = { const_cast<unsigned char*>(&image[y * stride]) };
    jpeg_write_scanlines(&cinfo, row, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  std::vector<unsigned char> result(outbuf, outbuf + outsize);
  free(outbuf);  // jpeg_mem_dest allocates with malloc

  return result;
}

} // namespace jpeg
} // namespace pdflib
