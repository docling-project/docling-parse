//-*-C++-*-

#ifndef PDF_TIMINGS_H
#define PDF_TIMINGS_H

#include <string>
#include <vector>
#include <utility>
#include <numeric>
#include <algorithm>
#include <functional>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>

namespace pdflib
{

  /**
   * @brief A class for tracking timing measurements during PDF parsing.
   *
   * Unlike a simple std::unordered_map<std::string, double>, this class stores all timing
   * measurements as vectors, allowing the same key to be recorded multiple times
   * (e.g., when a task is repeated). This provides more accurate profiling data.
   */
  class pdf_timings
  {
  public:
    // Static timing keys (constant across all PDFs)
    // Page-level timing keys
    static const std::string KEY_DECODE_PAGE;
    static const std::string KEY_DECODE_DIMENSIONS;
    static const std::string KEY_DECODE_RESOURCES;
    static const std::string KEY_DECODE_GRPHS;
    static const std::string KEY_DECODE_FONTS;
    static const std::string KEY_DECODE_XOBJECTS;
    static const std::string KEY_DECODE_CONTENTS;
    static const std::string KEY_DECODE_ANNOTS;
    static const std::string KEY_SANITISE_CONTENTS;
    static const std::string KEY_CREATE_WORD_CELLS;
    static const std::string KEY_CREATE_LINE_CELLS;

    // Additional decode_page step keys
    static const std::string KEY_TO_JSON_PAGE;
    static const std::string KEY_EXTRACT_ANNOTS_JSON;
    static const std::string KEY_ROTATE_CONTENTS;
    static const std::string KEY_SANITIZE_ORIENTATION;
    static const std::string KEY_SANITIZE_CELLS;

    // Font timing keys
    static const std::string KEY_DECODE_FONTS_TOTAL;

    // Font sub-timing categories (aggregated across all fonts)
    static const std::string KEY_FONT_INIT_COPY;
    static const std::string KEY_FONT_INIT_METRICS;
    static const std::string KEY_FONT_CMAP;
    static const std::string KEY_FONT_CMAP_STREAM_DECODE;
    static const std::string KEY_FONT_CMAP_RESOURCES;
    static const std::string KEY_FONT_CHARS;

    // XObject timing keys
    static const std::string KEY_DECODE_XOBJECTS_TOTAL;

    // Form xobject stream decompression + tokenization (do_form -> parse_stream)
    static const std::string KEY_PARSE_STREAM_TOTAL;

    // Form xobject machinery: child-resource allocation, graphics-state copies
    // (q()/Q()), stack copy (update_stack) -- i.e. do_form cost that is NOT
    // resource-parsing, stream-decoding or nested interpretation.
    static const std::string KEY_DO_FORM_MACHINERY;

    // Image xobject drawing/extraction (do_image -> Do_image)
    static const std::string KEY_DO_IMAGE_TOTAL;

    // Top-level page content-stream tokenization (decode_contents -> decode)
    static const std::string KEY_CONTENT_DECODE_TOTAL;

    // Operator-execution self-time: time spent interpreting operators across
    // all (recursion) levels, excluding the sub-work bucketed under the keys
    // above. Derived as a residual in decode_contents.
    static const std::string KEY_INTERPRETE_OPS_TOTAL;

    // Grphs timing keys
    static const std::string KEY_DECODE_GRPHS_TOTAL;

    // Document-level timing keys
    static const std::string KEY_PROCESS_DOCUMENT_FROM_FILE;
    static const std::string KEY_PROCESS_DOCUMENT_FROM_BYTESIO;
    static const std::string KEY_QPDF_PROCESS;
    static const std::string KEY_QPDF_BUILD_THREAD_SAFE_BUFFER;
    static const std::string KEY_EXTRACT_DOC_ANNOTATIONS;
    static const std::string KEY_DECODE_DOCUMENT;
    static const std::string KEY_PROCESS_DOCUMENT_COMPONENTS;
    
    // Dynamic key prefixes (for pattern matching)
    static const std::string PREFIX_DECODE_FONT;
    static const std::string PREFIX_DECODE_XOBJECT;
    static const std::string PREFIX_DECODE_GRPH;
    static const std::string PREFIX_DECODING_PAGE;
    static const std::string PREFIX_DECODE_PAGE;

    // CMap parsing timing keys
    static const std::string KEY_CMAP_PARSE_TOTAL;
    static const std::string KEY_CMAP_PARSE_ENDBFCHAR;
    static const std::string KEY_CMAP_PARSE_ENDBFRANGE;
    static const std::string KEY_CMAP_PARSE_ENDCODESPACERANGE;

    /**
     * @brief Get all static timing keys.
     */
    static const std::unordered_set<std::string>& get_static_keys();

    /**
     * @brief Check if a key is a static (constant) timing key.
     */
    static bool is_static_key(const std::string& key);

    /**
     * @brief Get static timing keys used in decode_page method (excluding the global timer).
     * @return Keys in the same order as they appear in decode_page.
     */
    static const std::vector<std::string>& get_decode_page_keys();

    /**
     * @brief Get the parent timing key under which `key` is accounted.
     *
     * Returns the key that `key` is a sub-timing of (e.g.
     * KEY_DECODE_XOBJECTS_TOTAL -> KEY_DECODE_CONTENTS), or "" when `key`
     * is a top-level (root) key. Handles both static keys and dynamic
     * per-item keys (e.g. "decode_xobject: /Tr1624" -> decode_xobjects_total).
     *
     * NOTE: the *_total keys (fonts/xobjects/grphs) are physically
     * accumulated across decode_resources, decode_contents and decode_annots.
     * For a single-parent tree they are attributed to their typically
     * dominant parent, decode_contents.
     */
    static std::string get_parent_key(const std::string& key);

    /**
     * @brief Get the canonical containment tree of timing keys.
     *
     * Maps a parent key to its ordered list of child keys. The synthetic
     * root "" holds the top-level keys. Intended for rendering a nested
     * (tree-shaped) timings table.
     */
    static const std::unordered_map<std::string, std::vector<std::string>>& get_key_tree();

    /**
     * @brief Render the timings as a nested tree table.
     *
     * Produces a table where:
     *   - each tree level is its own column (L0, L1, ... Ln),
     *   - "time[s]" is the summed time accounted to that key,
     *   - "%branch" is the key's share within its branch, i.e. relative to
     *     the sum of all siblings sharing the same parent.
     *
     * Keys present in `sums` but unknown to the tree are listed at the top
     * level so nothing is silently dropped.
     *
     * @param sums       key -> summed time (e.g. from to_sum_map()).
     * @param total_time optional wall-clock total appended as a final row
     *                   (pass a negative value to omit it).
     */
    static std::string format_tree_table(const std::unordered_map<std::string, double>& sums,
                                          double total_time = -1.0);

    /**
     * @brief Accountancy used to derive operator self-time.
     *
     * Bucketed sub-tasks that run *inside* the interpretation loop
     * (resource set(), parse_stream, do_image, do_form machinery) call
     * note_attributed() with their measured duration. decode_contents reads
     * attributed_total() before/after interpreting to compute the
     * operator-execution self-time as: interprete_total - attributed_delta.
     *
     * This works because a single pdf_timings instance is threaded by
     * reference through the page decoder and all (recursive) stream decoders.
     */
    void   note_attributed(double seconds);
    double attributed_total() const;

    pdf_timings();
    ~pdf_timings();

    // Copy and move semantics
    pdf_timings(const pdf_timings&) = default;
    pdf_timings& operator=(const pdf_timings&) = default;
    pdf_timings(pdf_timings&&) = default;
    pdf_timings& operator=(pdf_timings&&) = default;

    /**
     * @brief Add a timing measurement for a given key.
     */
    void add_timing(const std::string& key, double value);

    /**
     * @brief Get the sum of all timing values for a given key.
     */
    double get_sum(const std::string& key) const;

    /**
     * @brief Get the count of timing measurements for a given key.
     */
    size_t get_count(const std::string& key) const;

    /**
     * @brief Get the average of all timing values for a given key.
     */
    double get_average(const std::string& key) const;

    /**
     * @brief Get all timing values for a given key.
     */
    const std::vector<double>& get_values(const std::string& key) const;

    /**
     * @brief Check if a key exists in the timings.
     */
    bool has_key(const std::string& key) const;

    /**
     * @brief Get an iterator to the beginning of the internal map.
     */
    auto begin() const { return timings_.begin(); }

    /**
     * @brief Get an iterator to the end of the internal map.
     */
    auto end() const { return timings_.end(); }

    /**
     * @brief Get the number of unique keys in the timings.
     */
    size_t size() const;

    /**
     * @brief Check if the timings are empty.
     */
    bool empty() const;

    /**
     * @brief Clear all timing data.
     */
    void clear();

    /**
     * @brief Merge another pdf_timings object into this one.
     */
    void merge(const pdf_timings& other);

    /**
     * @brief Convert to a map of sums for backward compatibility.
     */
    std::unordered_map<std::string, double> to_sum_map() const;

    /**
     * @brief Get the raw internal map for direct access.
     */
    const std::unordered_map<std::string, std::vector<double>>& get_raw_data() const;

    /**
     * @brief Get only the static timing keys that are present.
     */
    std::unordered_map<std::string, double> get_static_timings() const;

    /**
     * @brief Get only the dynamic timing keys that are present.
     */
    std::unordered_map<std::string, double> get_dynamic_timings() const;

  private:
    /**
     * @brief Canonical (child, parent) containment pairs; single source of
     *        truth for both get_parent_key() and get_key_tree(). Ordered so
     *        that children appear under their parent in a sensible order.
     */
    static const std::vector<std::pair<std::string, std::string>>& get_containment_pairs();

    // running total of time attributed to bucketed sub-tasks
    double attributed_seconds_ = 0.0;

    std::unordered_map<std::string, std::vector<double>> timings_;
  };

  // Implementation

  pdf_timings::pdf_timings()
  {}

  pdf_timings::~pdf_timings()
  {}

  void pdf_timings::add_timing(const std::string& key, double value)
  {
    timings_[key].push_back(value);
  }

  double pdf_timings::get_sum(const std::string& key) const
  {
    auto it = timings_.find(key);
    if (it == timings_.end())
      {
        return 0.0;
      }
    return std::accumulate(it->second.begin(), it->second.end(), 0.0);
  }

  size_t pdf_timings::get_count(const std::string& key) const
  {
    auto it = timings_.find(key);
    if (it == timings_.end())
      {
        return 0;
      }
    return it->second.size();
  }

  double pdf_timings::get_average(const std::string& key) const
  {
    auto it = timings_.find(key);
    if (it == timings_.end() || it->second.empty())
      {
        return 0.0;
      }
    return get_sum(key) / static_cast<double>(it->second.size());
  }

  const std::vector<double>& pdf_timings::get_values(const std::string& key) const
  {
    static const std::vector<double> empty_vec;
    auto it = timings_.find(key);
    if (it == timings_.end())
      {
        return empty_vec;
      }
    return it->second;
  }

  bool pdf_timings::has_key(const std::string& key) const
  {
    return timings_.find(key) != timings_.end();
  }

  size_t pdf_timings::size() const
  {
    return timings_.size();
  }

  bool pdf_timings::empty() const
  {
    return timings_.empty();
  }

  void pdf_timings::clear()
  {
    timings_.clear();
  }

  void pdf_timings::merge(const pdf_timings& other)
  {
    for (const auto& pair : other.timings_)
      {
        auto& vec = timings_[pair.first];
        vec.insert(vec.end(), pair.second.begin(), pair.second.end());
      }
  }

  std::unordered_map<std::string, double> pdf_timings::to_sum_map() const
  {
    std::unordered_map<std::string, double> result;
    for (const auto& pair : timings_)
      {
        result[pair.first] = std::accumulate(pair.second.begin(), pair.second.end(), 0.0);
      }
    return result;
  }

  const std::unordered_map<std::string, std::vector<double>>& pdf_timings::get_raw_data() const
  {
    return timings_;
  }

  std::unordered_map<std::string, double> pdf_timings::get_static_timings() const
  {
    std::unordered_map<std::string, double> result;
    for (const auto& pair : timings_)
      {
        if (is_static_key(pair.first))
          {
            result[pair.first] = std::accumulate(pair.second.begin(), pair.second.end(), 0.0);
          }
      }
    return result;
  }

  std::unordered_map<std::string, double> pdf_timings::get_dynamic_timings() const
  {
    std::unordered_map<std::string, double> result;
    for (const auto& pair : timings_)
      {
        if (!is_static_key(pair.first))
          {
            result[pair.first] = std::accumulate(pair.second.begin(), pair.second.end(), 0.0);
          }
      }
    return result;
  }

  // Static constant definitions
  const std::string pdf_timings::KEY_DECODE_PAGE = "decode_page";
  const std::string pdf_timings::KEY_DECODE_DIMENSIONS = "decode_dimensions";
  const std::string pdf_timings::KEY_DECODE_RESOURCES = "decode_resources";
  const std::string pdf_timings::KEY_DECODE_GRPHS = "decode_grphs";
  const std::string pdf_timings::KEY_DECODE_FONTS = "decode_fonts";
  const std::string pdf_timings::KEY_DECODE_XOBJECTS = "decode_xobjects";
  const std::string pdf_timings::KEY_DECODE_CONTENTS = "decode_contents";
  const std::string pdf_timings::KEY_DECODE_ANNOTS = "decode_annots";
  const std::string pdf_timings::KEY_SANITISE_CONTENTS = "sanitise_contents";
  const std::string pdf_timings::KEY_CREATE_WORD_CELLS = "create_word_cells";
  const std::string pdf_timings::KEY_CREATE_LINE_CELLS = "create_line_cells";

  const std::string pdf_timings::KEY_DECODE_FONTS_TOTAL = "decode_fonts_total";
  const std::string pdf_timings::KEY_FONT_INIT_COPY = "font: init-copy";
  const std::string pdf_timings::KEY_FONT_INIT_METRICS = "font: init-metrics";
  const std::string pdf_timings::KEY_FONT_CMAP = "font: font-cmap";
  const std::string pdf_timings::KEY_FONT_CMAP_STREAM_DECODE = "font: font-cmap-stream-decode";
  const std::string pdf_timings::KEY_FONT_CMAP_RESOURCES = "font: font-cmap-resources";
  const std::string pdf_timings::KEY_FONT_CHARS = "font: font-chars";
  const std::string pdf_timings::KEY_DECODE_XOBJECTS_TOTAL = "decode_xobjects_total";
  const std::string pdf_timings::KEY_PARSE_STREAM_TOTAL = "parse_stream_total";
  const std::string pdf_timings::KEY_DO_FORM_MACHINERY = "do_form_machinery_total";
  const std::string pdf_timings::KEY_DO_IMAGE_TOTAL = "do_image_total";
  const std::string pdf_timings::KEY_CONTENT_DECODE_TOTAL = "content_decode_total";
  const std::string pdf_timings::KEY_INTERPRETE_OPS_TOTAL = "interprete_ops_total";
  const std::string pdf_timings::KEY_DECODE_GRPHS_TOTAL = "decode_grphs_total";

  // Additional decode_page step keys
  const std::string pdf_timings::KEY_TO_JSON_PAGE = "to_json_page";
  const std::string pdf_timings::KEY_EXTRACT_ANNOTS_JSON = "extract_annots_json";
  const std::string pdf_timings::KEY_ROTATE_CONTENTS = "rotate_contents";
  const std::string pdf_timings::KEY_SANITIZE_ORIENTATION = "sanitize_orientation";
  const std::string pdf_timings::KEY_SANITIZE_CELLS = "sanitize_cells";

  const std::string pdf_timings::KEY_PROCESS_DOCUMENT_FROM_FILE = "process_document_from_file";
  const std::string pdf_timings::KEY_PROCESS_DOCUMENT_FROM_BYTESIO = "process_document_from_bytesio";
  const std::string pdf_timings::KEY_QPDF_PROCESS = "qpdf_process";
  const std::string pdf_timings::KEY_QPDF_BUILD_THREAD_SAFE_BUFFER = "qpdf_build_thread_safe_buffer";
  const std::string pdf_timings::KEY_EXTRACT_DOC_ANNOTATIONS = "extract_doc_annotations";
  const std::string pdf_timings::KEY_DECODE_DOCUMENT = "decode_document";
  const std::string pdf_timings::KEY_PROCESS_DOCUMENT_COMPONENTS = "decode_document_components";
  
  const std::string pdf_timings::PREFIX_DECODE_FONT = "decode_font: ";
  const std::string pdf_timings::PREFIX_DECODE_XOBJECT = "decode_xobject: ";
  const std::string pdf_timings::PREFIX_DECODE_GRPH = "decode_grph: ";
  const std::string pdf_timings::PREFIX_DECODING_PAGE = "decoding page ";
  const std::string pdf_timings::PREFIX_DECODE_PAGE = "decode_page ";

  // CMap parsing timing keys (aggregated across all fonts)
  const std::string pdf_timings::KEY_CMAP_PARSE_TOTAL = "cmap-parse-total";
  const std::string pdf_timings::KEY_CMAP_PARSE_ENDBFCHAR = "cmap-parse-endbfchar";
  const std::string pdf_timings::KEY_CMAP_PARSE_ENDBFRANGE = "cmap-parse-endbfrange";
  const std::string pdf_timings::KEY_CMAP_PARSE_ENDCODESPACERANGE = "cmap-parse-endcodespacerange";

  const std::unordered_set<std::string>& pdf_timings::get_static_keys()
  {
    static std::unordered_set<std::string> static_keys = {
      KEY_DECODE_PAGE,
      KEY_DECODE_DIMENSIONS,
      KEY_DECODE_RESOURCES,
      KEY_DECODE_GRPHS,
      KEY_DECODE_FONTS,
      KEY_DECODE_XOBJECTS,
      KEY_DECODE_CONTENTS,
      KEY_DECODE_ANNOTS,
      KEY_SANITISE_CONTENTS,
      KEY_CREATE_WORD_CELLS,
      KEY_CREATE_LINE_CELLS,
      KEY_DECODE_FONTS_TOTAL,
      KEY_FONT_INIT_COPY,
      KEY_FONT_INIT_METRICS,
      KEY_FONT_CMAP,
      KEY_FONT_CMAP_STREAM_DECODE,
      KEY_FONT_CMAP_RESOURCES,
      KEY_FONT_CHARS,
      KEY_CMAP_PARSE_TOTAL,
      KEY_CMAP_PARSE_ENDBFCHAR,
      KEY_CMAP_PARSE_ENDBFRANGE,
      KEY_CMAP_PARSE_ENDCODESPACERANGE,
      KEY_DECODE_XOBJECTS_TOTAL,
      KEY_PARSE_STREAM_TOTAL,
      KEY_DO_FORM_MACHINERY,
      KEY_DO_IMAGE_TOTAL,
      KEY_CONTENT_DECODE_TOTAL,
      KEY_INTERPRETE_OPS_TOTAL,
      KEY_DECODE_GRPHS_TOTAL,
      KEY_TO_JSON_PAGE,
      KEY_EXTRACT_ANNOTS_JSON,
      KEY_ROTATE_CONTENTS,
      KEY_SANITIZE_ORIENTATION,
      KEY_SANITIZE_CELLS,
      KEY_PROCESS_DOCUMENT_FROM_FILE,
      KEY_PROCESS_DOCUMENT_FROM_BYTESIO,
      KEY_QPDF_PROCESS,
      KEY_QPDF_BUILD_THREAD_SAFE_BUFFER,
      KEY_EXTRACT_DOC_ANNOTATIONS,
      KEY_DECODE_DOCUMENT
    };
    return static_keys;
  }

  bool pdf_timings::is_static_key(const std::string& key)
  {
    const auto& static_keys = get_static_keys();
    return static_keys.find(key) != static_keys.end();
  }

  const std::vector<std::string>& pdf_timings::get_decode_page_keys()
  {
    // Keys in the same order as they appear in decode_page method
    static std::vector<std::string> decode_page_keys = {
      KEY_TO_JSON_PAGE,
      KEY_EXTRACT_ANNOTS_JSON,
      KEY_DECODE_DIMENSIONS,
      KEY_DECODE_RESOURCES,
      KEY_DECODE_CONTENTS,
      KEY_DECODE_ANNOTS,
      KEY_ROTATE_CONTENTS,
      KEY_SANITIZE_ORIENTATION,
      KEY_SANITIZE_CELLS,
      KEY_SANITISE_CONTENTS
    };
    return decode_page_keys;
  }

  const std::vector<std::pair<std::string, std::string>>& pdf_timings::get_containment_pairs()
  {
    // (child, parent) pairs. Parent "" denotes a top-level (root) key.
    //
    // The *_total keys (fonts/xobjects/grphs) are physically accumulated
    // across decode_resources, decode_contents and decode_annots; for a
    // single-parent tree they are attributed to decode_contents, which is
    // their dominant parent in practice.
    static const std::vector<std::pair<std::string, std::string>> pairs = {
      // --- document loading ---
      {KEY_PROCESS_DOCUMENT_FROM_FILE,     ""},
      {KEY_PROCESS_DOCUMENT_FROM_BYTESIO,  KEY_PROCESS_DOCUMENT_FROM_FILE},
      {KEY_QPDF_PROCESS,                   KEY_PROCESS_DOCUMENT_FROM_BYTESIO},
      {KEY_QPDF_BUILD_THREAD_SAFE_BUFFER,  KEY_PROCESS_DOCUMENT_FROM_BYTESIO},
      {KEY_EXTRACT_DOC_ANNOTATIONS,        KEY_PROCESS_DOCUMENT_FROM_BYTESIO},

      // --- page decoding ---
      {KEY_DECODE_DOCUMENT,                ""},
      {KEY_PROCESS_DOCUMENT_COMPONENTS,    KEY_DECODE_DOCUMENT},
      {KEY_DECODE_PAGE,                    KEY_DECODE_DOCUMENT},

      {KEY_TO_JSON_PAGE,                   KEY_DECODE_PAGE},
      {KEY_EXTRACT_ANNOTS_JSON,            KEY_DECODE_PAGE},
      {KEY_DECODE_DIMENSIONS,              KEY_DECODE_PAGE},
      {KEY_DECODE_RESOURCES,               KEY_DECODE_PAGE},
      {KEY_DECODE_CONTENTS,                KEY_DECODE_PAGE},
      {KEY_DECODE_ANNOTS,                  KEY_DECODE_PAGE},
      {KEY_ROTATE_CONTENTS,                KEY_DECODE_PAGE},
      {KEY_SANITIZE_ORIENTATION,           KEY_DECODE_PAGE},
      {KEY_SANITIZE_CELLS,                 KEY_DECODE_PAGE},
      {KEY_SANITISE_CONTENTS,              KEY_DECODE_PAGE},

      // --- decode_contents sub-timings ---
      {KEY_CONTENT_DECODE_TOTAL,           KEY_DECODE_CONTENTS},
      {KEY_INTERPRETE_OPS_TOTAL,           KEY_DECODE_CONTENTS},
      {KEY_DECODE_XOBJECTS_TOTAL,          KEY_DECODE_CONTENTS},
      {KEY_DECODE_GRPHS_TOTAL,             KEY_DECODE_CONTENTS},
      {KEY_DECODE_FONTS_TOTAL,             KEY_DECODE_CONTENTS},
      {KEY_PARSE_STREAM_TOTAL,             KEY_DECODE_CONTENTS},
      {KEY_DO_FORM_MACHINERY,              KEY_DECODE_CONTENTS},
      {KEY_DO_IMAGE_TOTAL,                 KEY_DECODE_CONTENTS},

      // --- sanitise_contents sub-timings ---
      {KEY_CREATE_WORD_CELLS,              KEY_SANITISE_CONTENTS},
      {KEY_CREATE_LINE_CELLS,              KEY_SANITISE_CONTENTS},

      // --- font sub-timings (aggregated across fonts) ---
      {KEY_FONT_INIT_COPY,                 KEY_DECODE_FONTS_TOTAL},
      {KEY_FONT_INIT_METRICS,              KEY_DECODE_FONTS_TOTAL},
      {KEY_FONT_CMAP,                      KEY_DECODE_FONTS_TOTAL},
      {KEY_FONT_CMAP_STREAM_DECODE,        KEY_FONT_CMAP},
      {KEY_FONT_CMAP_RESOURCES,            KEY_DECODE_FONTS_TOTAL},
      {KEY_FONT_CHARS,                     KEY_DECODE_FONTS_TOTAL},

      // --- font cmap parsing sub-timings (live under font: font-cmap) ---
      {KEY_CMAP_PARSE_TOTAL,               KEY_FONT_CMAP},
      {KEY_CMAP_PARSE_ENDBFCHAR,           KEY_CMAP_PARSE_TOTAL},
      {KEY_CMAP_PARSE_ENDBFRANGE,          KEY_CMAP_PARSE_TOTAL},
      {KEY_CMAP_PARSE_ENDCODESPACERANGE,   KEY_CMAP_PARSE_TOTAL},
    };
    return pairs;
  }

  void pdf_timings::note_attributed(double seconds)
  {
    attributed_seconds_ += seconds;
  }

  double pdf_timings::attributed_total() const
  {
    return attributed_seconds_;
  }

  std::string pdf_timings::get_parent_key(const std::string& key)
  {
    // Dynamic per-item keys roll up into their corresponding *_total parent.
    auto starts_with = [&key](const std::string& prefix)
      {
        return key.size()>=prefix.size() and key.compare(0, prefix.size(), prefix)==0;
      };

    if(starts_with(PREFIX_DECODE_FONT))    { return KEY_DECODE_FONTS_TOTAL; }
    if(starts_with(PREFIX_DECODE_XOBJECT)) { return KEY_DECODE_XOBJECTS_TOTAL; }
    if(starts_with(PREFIX_DECODE_GRPH))    { return KEY_DECODE_GRPHS_TOTAL; }

    static const std::unordered_map<std::string, std::string> parent_of = []
      {
        std::unordered_map<std::string, std::string> m;
        for(const auto& pair : get_containment_pairs())
          {
            m[pair.first] = pair.second;
          }
        return m;
      }();

    auto itr = parent_of.find(key);
    return itr!=parent_of.end() ? itr->second : std::string();
  }

  const std::unordered_map<std::string, std::vector<std::string>>& pdf_timings::get_key_tree()
  {
    static const std::unordered_map<std::string, std::vector<std::string>> tree = []
      {
        std::unordered_map<std::string, std::vector<std::string>> t;
        for(const auto& pair : get_containment_pairs())
          {
            t[pair.second].push_back(pair.first); // parent -> child (ordered)
          }
        return t;
      }();
    return tree;
  }

  std::string pdf_timings::format_tree_table(const std::unordered_map<std::string, double>& sums,
                                             double total_time)
  {
    const auto& tree = get_key_tree();

    auto sum_of = [&sums](const std::string& key) -> double
      {
        auto itr = sums.find(key);
        return itr!=sums.end() ? itr->second : 0.0;
      };

    auto children_of = [&tree](const std::string& key) -> const std::vector<std::string>*
      {
        auto itr = tree.find(key);
        return itr!=tree.end() ? &itr->second : nullptr;
      };

    // A node is shown if it -- or any of its descendants -- has a measured time.
    std::function<bool(const std::string&)> has_data =
      [&](const std::string& key) -> bool
      {
        if(sums.find(key)!=sums.end()) { return true; }
        if(const auto* ch = children_of(key))
          {
            for(const auto& c : *ch) { if(has_data(c)) { return true; } }
          }
        return false;
      };

    // Collect rows in depth-first (tree) order: (depth, key).
    std::vector<std::pair<int, std::string>> rows;
    std::function<void(const std::string&, int)> dfs =
      [&](const std::string& key, int depth) -> void
      {
        if(const auto* ch = children_of(key))
          {
            for(const auto& c : *ch)
              {
                if(not has_data(c)) { continue; }
                rows.push_back({depth, c});
                dfs(c, depth+1);
              }
          }
      };
    dfs("", 0);

    // Static keys present in sums but unknown to the tree: surface them at top
    // level so a measured timing is never silently dropped. Dynamic per-item
    // keys (e.g. "decode_xobject: /Tr123" or "decode_page 0") are intentionally
    // skipped here -- they are not part of the static tree.
    {
      static const std::unordered_set<std::string> known = []
        {
          std::unordered_set<std::string> s;
          for(const auto& pair : get_containment_pairs()) { s.insert(pair.first); }
          return s;
        }();
      for(const auto& kv : sums)
        {
          if(is_static_key(kv.first) and known.find(kv.first)==known.end())
            {
              rows.push_back({0, kv.first});
            }
        }
    }

    // %branch = node time / sum of sibling times (children sharing a parent).
    auto branch_pct = [&](const std::string& key) -> double
      {
        std::string parent = get_parent_key(key);
        double denom = 0.0;
        if(const auto* ch = children_of(parent))
          {
            for(const auto& c : *ch) { if(has_data(c)) { denom += sum_of(c); } }
          }
        return denom>0.0 ? 100.0*sum_of(key)/denom : 0.0;
      };

    // Per-level column widths, sized to their content.
    int max_depth = 0;
    for(const auto& r : rows) { max_depth = std::max(max_depth, r.first); }

    std::vector<size_t> width(max_depth+1, 4);
    for(const auto& r : rows) { width[r.first] = std::max(width[r.first], r.second.size()); }

    const int time_w = 14;
    const int pct_w  = 10;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(6);

    // header
    for(int d=0; d<=max_depth; d++)
      {
        ss << std::left << std::setw((int)width[d]+2) << ("L"+std::to_string(d));
      }
    ss << std::right << std::setw(time_w) << "time[s]"
       << std::setw(pct_w) << "%branch" << "\n";

    // rows
    for(const auto& r : rows)
      {
        int depth = r.first;
        const std::string& key = r.second;

        for(int d=0; d<=max_depth; d++)
          {
            ss << std::left << std::setw((int)width[d]+2) << (d==depth ? key : std::string());
          }

        ss << std::right << std::setw(time_w) << sum_of(key);

        std::stringstream pct;
        pct << std::fixed << std::setprecision(1) << branch_pct(key) << "%";
        ss << std::setw(pct_w) << pct.str() << "\n";
      }

    // wall-clock total
    if(total_time>=0.0)
      {
        size_t name_span = 0;
        for(int d=0; d<=max_depth; d++) { name_span += width[d]+2; }

        ss << std::left << std::setw((int)name_span) << "total-time (wall clock)"
           << std::right << std::setw(time_w) << total_time << "\n";
      }

    return ss.str();
  }

}

#endif
