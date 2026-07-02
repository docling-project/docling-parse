//-*-C++-*-

#ifndef PDF_BLEND2D_EMBEDDED_FONT_CACHE_H
#define PDF_BLEND2D_EMBEDDED_FONT_CACHE_H

#include <blend2d/blend2d.h>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <parse/page_items/embedded_font_blob.h>

namespace pdflib
{
  // Loads BLFontFace objects from embedded PDF font programs and caches the
  // result (success or failure) by the blob's content-hash cache key. The
  // cache key is content-derived, so one cache instance can safely serve
  // multiple documents and threads.
  //
  // Blend2D only accepts SFNT containers (TrueType `0x00010000`/`true`,
  // OpenType `OTTO`, collections `ttcf`); bare CFF (/FontFile3 /Type1C,
  // /CIDFontType0C) and Type 1 (/FontFile) fail with
  // BL_ERROR_INVALID_SIGNATURE and are cached as failures so the renderer
  // falls back to system fonts without retrying per text cell.
  class blend2d_embedded_font_cache
  {
  public:

    blend2d_embedded_font_cache();

    // Returns the loaded face for the blob, or an empty/invalid face when the
    // blob is null, has no bytes, or Blend2D cannot load the format.
    BLFontFace resolve(const std::shared_ptr<const embedded_font_blob>& blob);

  private:

    struct cache_entry
    {
      // Keeps the byte buffer alive: BLFontData wraps the memory without
      // copying, so the bytes must outlive data/face.
      std::shared_ptr<const std::vector<uint8_t> > bytes;

      BLFontData data;
      BLFontFace face;

      bool failed = false;
    };

    BLFontFace load_face(const std::shared_ptr<const embedded_font_blob>& blob);

    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, cache_entry> cache_;
  };

  inline blend2d_embedded_font_cache::blend2d_embedded_font_cache() = default;

  inline BLFontFace blend2d_embedded_font_cache::resolve(
      const std::shared_ptr<const embedded_font_blob>& blob)
  {
    if(blob == nullptr or not blob->has_bytes())
      {
        return {};
      }

    {
      std::shared_lock lock(cache_mutex_);
      auto itr = cache_.find(blob->get_cache_key());
      if(itr != cache_.end())
        {
          return itr->second.face;
        }
    }

    return load_face(blob);
  }

  inline BLFontFace blend2d_embedded_font_cache::load_face(
      const std::shared_ptr<const embedded_font_blob>& blob)
  {
    cache_entry entry;
    entry.bytes = blob->get_bytes();

    const BLResult data_res = entry.data.create_from_data(
        entry.bytes->data(), entry.bytes->size());

    BLResult face_res = BL_ERROR_INVALID_DATA;
    if(data_res == BL_SUCCESS)
      {
        face_res = entry.face.create_from_data(entry.data, 0);
      }

    entry.failed = not (data_res == BL_SUCCESS and
                        face_res == BL_SUCCESS and
                        entry.face.is_valid());

    if(entry.failed)
      {
        // A failed load keeps no byte buffer alive.
        entry.bytes.reset();
        entry.data.reset();
        entry.face.reset();

        LOG_S(INFO) << "embedded font cache: load failed"
                    << " font_name=`" << blob->get_font_name() << "`"
                    << " source=" << blob->get_source_key()
                    << " format=" << to_string(blob->get_format())
                    << " bytes=" << blob->byte_size()
                    << " data_res=" << data_res
                    << " face_res=" << face_res
                    << " cache_key=" << blob->get_cache_key();
      }
    else
      {
        LOG_S(INFO) << "embedded font cache: loaded face"
                    << " font_name=`" << blob->get_font_name() << "`"
                    << " family=`" << entry.face.family_name().data() << "`"
                    << " source=" << blob->get_source_key()
                    << " format=" << to_string(blob->get_format())
                    << " bytes=" << blob->byte_size()
                    << " cache_key=" << blob->get_cache_key();
      }

    {
      std::unique_lock lock(cache_mutex_);
      auto [itr, inserted] = cache_.emplace(blob->get_cache_key(), std::move(entry));
      return itr->second.face;
    }
  }

}

#endif
