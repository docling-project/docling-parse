//-*-C++-*-

#ifndef PDF_PAGE_FONT_CMAP_VALUE_H
#define PDF_PAGE_FONT_CMAP_VALUE_H

namespace pdflib
{

  class cmap_value
  {

  public:

    cmap_value();

    cmap_value(std::unordered_map<uint32_t, std::string> map);

    cmap_value(bool is_identity,
               std::pair<uint32_t, uint32_t> range,
               std::unordered_map<uint32_t, std::string> map);

    bool is_identity() const;

    std::string at(uint32_t key) const;

    size_t count(uint32_t key) const;

    size_t size() const;

    bool empty() const;

    // Iteration (delegates to _map; for identity mode, _map is empty)
    std::unordered_map<uint32_t, std::string>::const_iterator begin() const;
    std::unordered_map<uint32_t, std::string>::const_iterator end() const;

  private:

    static std::string codepoint_to_utf8(uint32_t codepoint);

    bool _is_identity;
    std::pair<uint32_t, uint32_t> _identity_range;
    std::unordered_map<uint32_t, std::string> _map;
  };

  cmap_value::cmap_value():
    _is_identity(false),
    _identity_range({0, 0}),
    _map()
  {}

  cmap_value::cmap_value(std::unordered_map<uint32_t, std::string> map):
    _is_identity(false),
    _identity_range({0, 0}),
    _map(std::move(map))
  {}

  cmap_value::cmap_value(bool is_identity,
                         std::pair<uint32_t, uint32_t> range,
                         std::unordered_map<uint32_t, std::string> map):
    _is_identity(is_identity),
    _identity_range(range),
    _map(std::move(map))
  {}

  bool cmap_value::is_identity() const
  {
    return _is_identity;
  }

  std::string cmap_value::at(uint32_t key) const
  {
    // Map overrides take priority over identity
    if(_map.count(key) == 1)
      {
        return _map.at(key);
      }

    if(_is_identity and
       _identity_range.first <= key and key <= _identity_range.second)
      {
        return codepoint_to_utf8(key);
      }

    throw std::out_of_range("cmap_value::at: key " + std::to_string(key) + " not found");
  }

  size_t cmap_value::count(uint32_t key) const
  {
    if(_map.count(key) == 1)
      {
        return 1;
      }

    if(_is_identity and
       _identity_range.first <= key and key <= _identity_range.second)
      {
        return 1;
      }

    return 0;
  }

  size_t cmap_value::size() const
  {
    if(_is_identity)
      {
        return _identity_range.second - _identity_range.first + 1;
      }

    return _map.size();
  }

  bool cmap_value::empty() const
  {
    if(_is_identity)
      {
        return false;
      }

    return _map.empty();
  }

  std::unordered_map<uint32_t, std::string>::const_iterator cmap_value::begin() const
  {
    return _map.begin();
  }

  std::unordered_map<uint32_t, std::string>::const_iterator cmap_value::end() const
  {
    return _map.end();
  }

  std::string cmap_value::codepoint_to_utf8(uint32_t codepoint)
  {
    std::string result(4, 0);
    auto itr = utf8::append(codepoint, result.begin());
    result.erase(itr, result.end());
    return result;
  }

}

#endif
