#ifndef SRC_THIRD_PARTY_PDFIUM_JBIG2_H_
#define SRC_THIRD_PARTY_PDFIUM_JBIG2_H_

// Public interface for the vendored PDFium JBIG2 decoder.
// No PDFium types are exposed; callers only need standard C++ headers.
//
// Usage:
//   std::vector<uint8_t> bitmap = jbig2_decode(
//       {page_ptr, page_size},       // raw /JBIG2Decode stream bytes
//       {globals_ptr, globals_size}, // /JBIG2Globals bytes (empty span if absent)
//       width, height);              // from the PDF image dictionary
//
//   On success: returns a packed 1bpp bitmap,
//     pitch = (width + 7) / 8 bytes per row.
//     Bit 7 of byte 0 is the leftmost pixel; 0 = black, 1 = white.
//   On failure: returns an empty vector.

#include <cstdint>
#include <span>
#include <vector>

std::vector<uint8_t> jbig2_decode(
    std::span<const uint8_t> page_data,
    std::span<const uint8_t> globals_data,
    std::uint32_t width, std::uint32_t height);

#endif  // SRC_THIRD_PARTY_PDFIUM_JBIG2_H_
