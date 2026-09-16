//-*-C++-*-

#ifndef PDF_GLYPH_BBOX_CACHE_H
#define PDF_GLYPH_BBOX_CACHE_H

#include <blend2d/blend2d.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pdflib
{
  // A bounded, worker-local cache of glyph outline bounding boxes. A renderer
  // receives a cache at construction time so one worker can reuse the results
  // across its successive pages without synchronising with other workers.
  class glyph_bbox_cache
  {
  public:
    explicit glyph_bbox_cache(std::size_t capacity): capacity_(capacity)
    {
      heap_.reserve(capacity_);
    }

    // Returns false on a miss or when caching is disabled (capacity == 0).
    bool find(const std::string& face_key,
              uint32_t glyph_id,
              double font_size,
              BLBox& bbox)
    {
      if(capacity_ == 0) { return false; }

      const auto itr = entries_.find(make_key(face_key, glyph_id, font_size));
      if(itr == entries_.end()) { return false; }

      touch(itr);
      bbox = itr->second.bbox;
      return true;
    }

    void insert(const std::string& face_key,
                uint32_t glyph_id,
                double font_size,
                const BLBox& bbox)
    {
      if(capacity_ == 0) { return; }

      key cache_key = make_key(face_key, glyph_id, font_size);
      const auto existing = entries_.find(cache_key);
      if(existing != entries_.end())
        {
          existing->second.bbox = bbox;
          touch(existing);
          return;
        }

      if(entries_.size() >= capacity_)
        {
          // The heap root is the exact LFU victim. Recency breaks equal-use
          // ties, preventing an old one-hit entry from living forever.
          const key victim_key = *heap_.front()->cache_key;
          remove_root();
          entries_.erase(victim_key);
        }

      entry new_entry;
      new_entry.bbox = bbox;
      new_entry.use_count = 1;
      new_entry.last_used = ++clock_;
      const auto inserted = entries_.emplace(std::move(cache_key), new_entry);
      entry& value = inserted.first->second;
      value.cache_key = &inserted.first->first;
      value.heap_index = heap_.size();
      heap_.push_back(&value);
      sift_up(value.heap_index);
    }

  private:
    struct key
    {
      std::string face_key;
      uint32_t glyph_id = 0;
      uint64_t font_size_bits = 0;

      bool operator==(const key& other) const
      {
        return face_key == other.face_key &&
               glyph_id == other.glyph_id &&
               font_size_bits == other.font_size_bits;
      }
    };

    struct key_hash
    {
      std::size_t operator()(const key& value) const
      {
        const std::size_t h1 = std::hash<std::string>{}(value.face_key);
        const std::size_t h2 = std::hash<uint32_t>{}(value.glyph_id);
        const std::size_t h3 = std::hash<uint64_t>{}(value.font_size_bits);
        return h1 ^ (h2 << 1) ^ (h3 << 7);
      }
    };

    struct entry
    {
      BLBox bbox;
      uint64_t use_count = 0;
      uint64_t last_used = 0;
      const key* cache_key = nullptr;
      std::size_t heap_index = 0;
    };

    static key make_key(const std::string& face_key,
                        uint32_t glyph_id,
                        double font_size)
    {
      uint64_t font_size_bits = 0;
      static_assert(sizeof(font_size_bits) == sizeof(font_size));
      std::memcpy(&font_size_bits, &font_size, sizeof(font_size_bits));
      return key{face_key, glyph_id, font_size_bits};
    }

    void touch(std::unordered_map<key, entry, key_hash>::iterator itr)
    {
      entry& value = itr->second;
      ++value.use_count;
      value.last_used = ++clock_;
      // A hit always makes an entry less eligible for eviction.
      sift_down(value.heap_index);
    }

    static bool is_less_evictable(const entry* left, const entry* right)
    {
      return left->use_count != right->use_count
        ? left->use_count < right->use_count
        : left->last_used < right->last_used;
    }

    void swap_heap_entries(std::size_t left, std::size_t right)
    {
      std::swap(heap_[left], heap_[right]);
      heap_[left]->heap_index = left;
      heap_[right]->heap_index = right;
    }

    void sift_up(std::size_t index)
    {
      while(index > 0)
        {
          const std::size_t parent = (index - 1) / 2;
          if(not is_less_evictable(heap_[index], heap_[parent])) { break; }
          swap_heap_entries(index, parent);
          index = parent;
        }
    }

    void sift_down(std::size_t index)
    {
      while(true)
        {
          const std::size_t left = index * 2 + 1;
          if(left >= heap_.size()) { break; }

          std::size_t child = left;
          const std::size_t right = left + 1;
          if(right < heap_.size() &&
             is_less_evictable(heap_[right], heap_[left]))
            {
              child = right;
            }
          if(not is_less_evictable(heap_[child], heap_[index])) { break; }
          swap_heap_entries(index, child);
          index = child;
        }
    }

    void remove_root()
    {
      const std::size_t last = heap_.size() - 1;
      if(last > 0)
        {
          swap_heap_entries(0, last);
        }
      heap_.pop_back();
      if(not heap_.empty())
        {
          sift_down(0);
        }
    }

    std::size_t capacity_ = 0;
    uint64_t clock_ = 0;
    std::unordered_map<key, entry, key_hash> entries_;
    std::vector<entry*> heap_;
  };
}

#endif
