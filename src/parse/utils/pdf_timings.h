//-*-C++-*-

#ifndef PDF_TIMINGS_H
#define PDF_TIMINGS_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include <numeric>

namespace pdflib
{

  /**
   * @brief A class for tracking timing measurements during PDF parsing.
   *
   * Unlike a simple std::map<std::string, double>, this class stores all timing
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

    // Font timing keys
    static const std::string KEY_DECODE_FONTS_TOTAL;

    // Document-level timing keys
    static const std::string KEY_PROCESS_DOCUMENT_FROM_FILE;
    static const std::string KEY_PROCESS_DOCUMENT_FROM_BYTESIO;
    static const std::string KEY_DECODE_DOCUMENT;

    // Dynamic key prefixes (for pattern matching)
    static const std::string PREFIX_DECODE_FONT;
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
    static const std::set<std::string>& get_static_keys();

    /**
     * @brief Check if a key is a static (constant) timing key.
     */
    static bool is_static_key(const std::string& key);

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
    std::map<std::string, double> to_sum_map() const;

    /**
     * @brief Get the raw internal map for direct access.
     */
    const std::map<std::string, std::vector<double>>& get_raw_data() const;

    /**
     * @brief Get only the static timing keys that are present.
     */
    std::map<std::string, double> get_static_timings() const;

    /**
     * @brief Get only the dynamic timing keys that are present.
     */
    std::map<std::string, double> get_dynamic_timings() const;

  private:
    std::map<std::string, std::vector<double>> timings_;
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

  std::map<std::string, double> pdf_timings::to_sum_map() const
  {
    std::map<std::string, double> result;
    for (const auto& pair : timings_)
      {
        result[pair.first] = std::accumulate(pair.second.begin(), pair.second.end(), 0.0);
      }
    return result;
  }

  const std::map<std::string, std::vector<double>>& pdf_timings::get_raw_data() const
  {
    return timings_;
  }

  std::map<std::string, double> pdf_timings::get_static_timings() const
  {
    std::map<std::string, double> result;
    for (const auto& pair : timings_)
      {
        if (is_static_key(pair.first))
          {
            result[pair.first] = std::accumulate(pair.second.begin(), pair.second.end(), 0.0);
          }
      }
    return result;
  }

  std::map<std::string, double> pdf_timings::get_dynamic_timings() const
  {
    std::map<std::string, double> result;
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

  const std::string pdf_timings::KEY_PROCESS_DOCUMENT_FROM_FILE = "process_document_from_file";
  const std::string pdf_timings::KEY_PROCESS_DOCUMENT_FROM_BYTESIO = "process_document_from_bytesio";
  const std::string pdf_timings::KEY_DECODE_DOCUMENT = "decode_document";

  const std::string pdf_timings::PREFIX_DECODE_FONT = "decode_font: ";
  const std::string pdf_timings::PREFIX_DECODING_PAGE = "decoding page ";
  const std::string pdf_timings::PREFIX_DECODE_PAGE = "decode_page ";

  // CMap parsing timing keys
  const std::string pdf_timings::KEY_CMAP_PARSE_TOTAL = " cmap-parse-total";
  const std::string pdf_timings::KEY_CMAP_PARSE_ENDBFCHAR = " cmap-parse-endbfchar";
  const std::string pdf_timings::KEY_CMAP_PARSE_ENDBFRANGE = " cmap-parse-endbfrange";
  const std::string pdf_timings::KEY_CMAP_PARSE_ENDCODESPACERANGE = " cmap-parse-endcodespacerange";

  const std::set<std::string>& pdf_timings::get_static_keys()
  {
    static std::set<std::string> static_keys = {
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
      KEY_PROCESS_DOCUMENT_FROM_FILE,
      KEY_PROCESS_DOCUMENT_FROM_BYTESIO,
      KEY_DECODE_DOCUMENT
    };
    return static_keys;
  }

  bool pdf_timings::is_static_key(const std::string& key)
  {
    const auto& static_keys = get_static_keys();
    return static_keys.find(key) != static_keys.end();
  }

}

#endif
