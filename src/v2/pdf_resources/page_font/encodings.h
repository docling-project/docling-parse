//-*-C++-*-

#ifndef PDF_PAGE_FONT_ENCODINGS_H
#define PDF_PAGE_FONT_ENCODINGS_H

#include <mutex>
#include <atomic>

namespace pdflib
{

  class font_encodings
  {

  public:

    font_encodings();
    ~font_encodings();

    font_encoding& get(font_encoding_name name);

    template<typename glyphs_type>
    void initialise(std::string dirname, glyphs_type& glyphs);

  private:

    static std::atomic<bool> initialized;
    static std::mutex init_mutex;
    
    std::map<font_encoding_name, font_encoding> name_to_encoding;
  };

  // Static member definitions
  std::atomic<bool> font_encodings::initialized(false);
  std::mutex font_encodings::init_mutex;

  font_encodings::font_encodings()
  {}

  font_encodings::~font_encodings()
  {}

  font_encoding& font_encodings::get(font_encoding_name name)
  {
    return name_to_encoding.at(name);
  }    

  template<typename glyphs_type>
  void font_encodings::initialise(std::string dirname, glyphs_type& glyphs)
  {
    // Use double-checked locking pattern for thread-safe initialization
    if(initialized.load(std::memory_order_acquire))
      {
	LOG_S(WARNING) << "skipping font_encodings::initialise, already initialized ...";
	return;
      }
    
    std::lock_guard<std::mutex> lock(init_mutex);
    
    // Check again after acquiring lock
    if(initialized.load(std::memory_order_acquire))
      {
	LOG_S(WARNING) << "skipping font_encodings::initialise, already initialized ...";
	return;
      }
    
    std::vector<std::pair<font_encoding_name, std::string> > items = {
      {STANDARD, "std.dat"},
      {MACROMAN, "macroman.dat"},
      {MACEXPERT, "macexpert.dat"},
      {WINANSI, "winansi.dat"}
    };

    for(auto item:items)
      {
        font_encoding& encoding = name_to_encoding[item.first];
        encoding.initialise(item.first, dirname+"/"+item.second, glyphs);
      }

    initialized.store(true, std::memory_order_release);
  }

}

#endif
