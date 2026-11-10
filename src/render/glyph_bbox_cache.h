//-*-C++-*-

#ifndef PDF_GLYPH_BBOX_CACHE_H
#define PDF_GLYPH_BBOX_CACHE_H

#include <blend2d/blend2d.h>

#include <cstddef>
#include <cstdint>
#include <chrono>
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
    struct diagnostics
    {
      uint64_t lookups = 0, hits = 0, misses = 0;
      uint64_t insertions = 0, updates = 0, evictions = 0;
      uint64_t outline_attempts = 0, outline_successes = 0;
      double lookup_s = 0.0, hit_touch_s = 0.0;
      double insert_s = 0.0, outline_s = 0.0;
    };

    // Disabled scopes perform no clock reads or counter updates. Diagnostics
    // belong to this worker-local cache and require no synchronization.
    class scoped_measurement
    {
    public:
      scoped_measurement(glyph_bbox_cache& cache, double diagnostics::*slot):
        stats_(cache.diagnostics_enabled_ ? &cache.diagnostics_ : nullptr),
        slot_(slot),
        start_(stats_ ? clock_type::now() : clock_type::time_point{})
      {}

      ~scoped_measurement() { stop(); }
      scoped_measurement(const scoped_measurement&) = delete;
      scoped_measurement& operator=(const scoped_measurement&) = delete;

      void stop()
      {
        if(stats_)
          {
            stats_->*slot_ += std::chrono::duration<double>(clock_type::now() - start_).count();
            stats_ = nullptr;
          }
      }

      void outline_attempt() { if(stats_) { ++stats_->outline_attempts; } }
      void outline_success() { if(stats_) { ++stats_->outline_successes; } }

    private:
      using clock_type = std::chrono::steady_clock;
      diagnostics* stats_;
      double diagnostics::*slot_;
      clock_type::time_point start_;
    };

    explicit glyph_bbox_cache(std::size_t capacity, bool diagnostics_enabled = false):
      capacity_(capacity), diagnostics_enabled_(diagnostics_enabled)
    {
      heap_.reserve(capacity_);
    }

    diagnostics snapshot_diagnostics() const { return diagnostics_; }
    std::size_t size() const { return entries_.size(); }

    // Returns false on a miss or when caching is disabled (capacity == 0).
    bool find(const std::string& face_key,
              uint32_t glyph_id,
              double font_size,
              BLBox& bbox)
    {
      if(diagnostics_enabled_) { ++diagnostics_.lookups; }
      if(capacity_ == 0)
        {
          if(diagnostics_enabled_) { ++diagnostics_.misses; }
          return false;
        }

      scoped_measurement lookup(*this, &diagnostics::lookup_s);
      const auto itr = entries_.find(make_key(face_key, glyph_id, font_size));
      lookup.stop();
      if(itr == entries_.end())
        {
          if(diagnostics_enabled_) { ++diagnostics_.misses; }
          return false;
        }

      if(diagnostics_enabled_) { ++diagnostics_.hits; }
      {
        scoped_measurement hit_touch(*this, &diagnostics::hit_touch_s);
        touch(itr);
      }
      bbox = itr->second.bbox;
      return true;
    }

    void insert(const std::string& face_key,
                uint32_t glyph_id,
                double font_size,
                const BLBox& bbox)
    {
      if(capacity_ == 0) { return; }

      scoped_measurement insertion(*this, &diagnostics::insert_s);
      key cache_key = make_key(face_key, glyph_id, font_size);
      const auto existing = entries_.find(cache_key);
      if(existing != entries_.end())
        {
          if(diagnostics_enabled_) { ++diagnostics_.updates; }
          existing->second.bbox = bbox;
          touch(existing);
          return;
        }

      if(entries_.size() >= capacity_)
        {
          // The heap root is the least recently used entry, so glyphs from
          // earlier documents cannot retain slots on frequency alone.
          const key victim_key = *heap_.front()->cache_key;
          remove_root();
          entries_.erase(victim_key);
          if(diagnostics_enabled_) { ++diagnostics_.evictions; }
        }

      entry new_entry;
      new_entry.bbox = bbox;
      new_entry.use_count = 1;
      new_entry.last_used = ++clock_;
      const auto inserted = entries_.emplace(std::move(cache_key), new_entry);
      if(diagnostics_enabled_) { ++diagnostics_.insertions; }
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
      return left->last_used < right->last_used;
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
    bool diagnostics_enabled_ = false;
    diagnostics diagnostics_;
    uint64_t clock_ = 0;
    std::unordered_map<key, entry, key_hash> entries_;
    std::vector<entry*> heap_;
  };
}

#endif
