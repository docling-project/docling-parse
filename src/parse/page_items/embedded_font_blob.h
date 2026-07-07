//-*-C++-*-

#ifndef PAGE_ITEM_EMBEDDED_FONT_BLOB_H
#define PAGE_ITEM_EMBEDDED_FONT_BLOB_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <parse/enums.h>

namespace pdflib
{
  // Render-facing view of an embedded font program (/FontFile, /FontFile2,
  // /FontFile3). Deliberately free of qpdf and nlohmann types so that render
  // instructions and the renderer can carry it without pulling in the parse
  // dependencies. One blob is built per PDF font resource and shared (via
  // shared_ptr) by every text instruction that uses that font; the byte
  // buffer is immutable after construction.
  class embedded_font_blob
  {
  public:

    embedded_font_blob();
    embedded_font_blob(std::string cache_key,
                       std::string font_name,
                       std::string base_font,
                       std::string source_key,
                       embedded_font_format format,
                       bool is_cid_font,
                       bool cid_to_gid_identity,
                       bool uses_builtin_encoding,
                       std::shared_ptr<const std::vector<uint8_t> > bytes);

    // Content-derived identity (FNV-1a over the decoded bytes + size).
    // PDF object ids are NOT used: they are only unique within one document,
    // and the renderer-side face cache may outlive/serve many documents.
    const std::string& get_cache_key() const { return cache_key; }

    const std::string& get_font_name() const { return font_name; }
    const std::string& get_base_font() const { return base_font; }

    // "/FontFile", "/FontFile2" or "/FontFile3"
    const std::string& get_source_key() const { return source_key; }

    embedded_font_format get_format() const { return format; }

    // font belongs to a /Type0 composite font
    bool get_is_cid_font() const { return is_cid_font; }

    // /CIDToGIDMap is /Identity or absent
    bool get_cid_to_gid_identity() const { return cid_to_gid_identity; }

    // True for a simple font that is symbolic (/FontDescriptor /Flags bit 3)
    // and has no /Encoding override: its PDF character codes address the
    // font program's builtin cmap directly (PDF 32000-1, 9.6.6.4). Shaping
    // Unicode text against such a face mis-hits glyphs whenever a codepoint
    // collides with the font's code range (e.g. U+0020 landing on subset
    // code 0x20 of a Cairo/LibreOffice subset).
    bool get_uses_builtin_encoding() const { return uses_builtin_encoding; }

    const std::shared_ptr<const std::vector<uint8_t> >& get_bytes() const { return bytes; }

    bool has_bytes() const;
    size_t byte_size() const;

    static std::string compute_cache_key(const std::vector<uint8_t>& data);

  private:

    std::string cache_key;

    std::string font_name;
    std::string base_font;
    std::string source_key;

    embedded_font_format format;

    bool is_cid_font;
    bool cid_to_gid_identity;
    bool uses_builtin_encoding;

    std::shared_ptr<const std::vector<uint8_t> > bytes;
  };

  inline embedded_font_blob::embedded_font_blob():
    cache_key(),
    font_name(),
    base_font(),
    source_key(),
    format(embedded_font_format::UNKNOWN),
    is_cid_font(false),
    cid_to_gid_identity(false),
    uses_builtin_encoding(false),
    bytes(nullptr)
  {}

  inline embedded_font_blob::embedded_font_blob(std::string cache_key_,
                                                std::string font_name_,
                                                std::string base_font_,
                                                std::string source_key_,
                                                embedded_font_format format_,
                                                bool is_cid_font_,
                                                bool cid_to_gid_identity_,
                                                bool uses_builtin_encoding_,
                                                std::shared_ptr<const std::vector<uint8_t> > bytes_):
    cache_key(std::move(cache_key_)),
    font_name(std::move(font_name_)),
    base_font(std::move(base_font_)),
    source_key(std::move(source_key_)),
    format(format_),
    is_cid_font(is_cid_font_),
    cid_to_gid_identity(cid_to_gid_identity_),
    uses_builtin_encoding(uses_builtin_encoding_),
    bytes(std::move(bytes_))
  {}

  inline bool embedded_font_blob::has_bytes() const
  {
    return bytes != nullptr and not bytes->empty();
  }

  inline size_t embedded_font_blob::byte_size() const
  {
    return bytes ? bytes->size() : 0;
  }

  inline std::string embedded_font_blob::compute_cache_key(const std::vector<uint8_t>& data)
  {
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a 64 offset basis
    for(uint8_t byte : data)
      {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
      }

    static const char* digits = "0123456789abcdef";

    std::string key(16, '0');
    for(int i = 0; i < 16; i++)
      {
        key[15 - i] = digits[(hash >> (4 * i)) & 0xF];
      }

    return key + "-" + std::to_string(data.size());
  }

}

#endif
