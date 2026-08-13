//-*-C++-*-

#ifndef PDF_PAGE_FONTS_RESOURCE_H
#define PDF_PAGE_FONTS_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_FONTS>
  {
  public:

    pdf_resource();
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_FONTS>> parent);
    ~pdf_resource();

    nlohmann::json get();

    size_t size();

    int count(std::string key);

    std::unordered_set<std::string> keys();

    pdf_resource<PAGE_FONT>& operator[](std::string fort_name);

    void set(QPDFObjectHandle& qpdf_fonts_,
             pdf_timings& timings);

    // Walks a resource dictionary -- including the resources of every form
    // XObject beneath it -- and collects the richest /ToUnicode table per base
    // font into a registry shared across this page's whole font tree. See
    // share_cmaps_between_subset_siblings for why sharing is sound; the
    // registry closes the gap that pass cannot reach: siblings living in
    // DIFFERENT resource dictionaries (each form XObject keying its font /F0),
    // which are never present together in one container.
    void harvest_to_unicode(QPDFObjectHandle resources, pdf_timings& timings);

  private:

    // Strips the leading slash and a six-uppercase-letter subset prefix
    // ("ABCDEF+Times" -> "Times").
    static std::string base_font_key(std::string name);

    // Lets fonts that are subsets of the same base font share their /ToUnicode
    // tables. See the definition for why this is sound and what it repairs.
    void share_cmaps_between_subset_siblings();

    std::shared_ptr<pdf_resource<PAGE_FONTS>> parent_;
    std::unordered_map<std::string, pdf_resource<PAGE_FONT> > page_fonts;

    // base font name -> richest Identity-H/V /ToUnicode seen anywhere on the
    // page. Shared by pointer with every child PAGE_FONTS, so fonts decoded
    // inside form XObjects consult the same registry.
    std::shared_ptr<std::unordered_map<std::string, cmap_value> > tounicode_registry_;
  };

  pdf_resource<PAGE_FONTS>::pdf_resource():
    parent_(nullptr),
    tounicode_registry_(std::make_shared<std::unordered_map<std::string, cmap_value> >())
  {}

  pdf_resource<PAGE_FONTS>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_FONTS>> parent):
    parent_(parent),
    tounicode_registry_(parent ? parent->tounicode_registry_
                               : std::make_shared<std::unordered_map<std::string, cmap_value> >())
  {}

  pdf_resource<PAGE_FONTS>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_FONTS>::get()
  {
    nlohmann::json result;
    {
      for(auto itr=page_fonts.begin(); itr!=page_fonts.end(); itr++)
        {
          result[itr->first] = (itr->second).get();
        }
    }
    
    return result;
  }

  size_t pdf_resource<PAGE_FONTS>::size()
  {
    return page_fonts.size();
  }

  int pdf_resource<PAGE_FONTS>::count(std::string key)
  {
    if(page_fonts.count(key)==1)
      {
        return 1;
      }
    if(parent_)
      {
        return parent_->count(key);
      }
    return 0;
  }

  std::unordered_set<std::string> pdf_resource<PAGE_FONTS>::keys()
  {
    std::unordered_set<std::string> keys_;

    if(parent_)
      {
        keys_ = parent_->keys();
      }

    for(auto itr=page_fonts.begin(); itr!=page_fonts.end(); itr++)
      {
        keys_.insert(itr->first);
      }

    return keys_;
  }

  pdf_resource<PAGE_FONT>& pdf_resource<PAGE_FONTS>::operator[](std::string font_name)
  {
    if(page_fonts.count(font_name)==1)
      {
        return page_fonts.at(font_name);
      }

    if(parent_)
      {
        return (*parent_)[font_name];
      }

    {
      std::stringstream ss;
      ss << "font_name [" << font_name << "] is not known: ";
      for(auto itr=page_fonts.begin(); itr!=page_fonts.end(); itr++)
        {
          if(itr==page_fonts.begin())
            {
              ss << itr->first;
            }
          else
            {
              ss << ", " << itr->first;
            }
        }

      throw std::logic_error(ss.str());
    }

    return (page_fonts.begin()->second);
  }
  
  void pdf_resource<PAGE_FONTS>::set(QPDFObjectHandle& qpdf_fonts,
                                     pdf_timings& timings)
  {
    LOG_S(INFO) << __FUNCTION__;

    double total_font_time = 0.0;

    for(auto& key : qpdf_fonts.getKeys())
      {
        LOG_S(INFO) << "decoding font: " << key;

	utils::timer font_timer;

	QPDFObjectHandle qpdf_font = qpdf_fonts.getKey(key);
	nlohmann::json json_font = to_json(qpdf_font);

	LOG_S(INFO) << json_font.dump(2);
	
	pdf_resource<PAGE_FONT> page_font(timings);
	page_font.set(key, json_font, qpdf_font);

	if(page_fonts.count(key)==1)
	  {
	    LOG_S(WARNING) << "We are overwriting a font!";
	    page_fonts.erase(key);
	  }

	// A font whose own /ToUnicode is poor adopts the richest table seen for
	// the same base font anywhere on the page (own entries win). This is what
	// turns raw-CID mojibake back into text when the rich sibling lives in a
	// different resource dictionary.
	// Only fonts WITHOUT an embedded program adopt from the registry. An
	// embedded subset renders by its own glyphs -- codes its /ToUnicode
	// misses were never resolved through Unicode, and a donor whose subset
	// ordering differs would corrupt the extracted text while fixing
	// nothing. A non-embedded Identity-H font has no glyph source at all,
	// so the donor is strictly better than emitting the raw code.
	if(tounicode_registry_ and
	   page_font.get_embedded_font_blob()==nullptr and
	   (page_font.get_encoding()==IDENTITY_H or page_font.get_encoding()==IDENTITY_V))
	  {
	    const std::string base = base_font_key(page_font.get_base_font());
	    auto found = tounicode_registry_->find(base);
	    if(found != tounicode_registry_->end())
	      {
		const size_t before = page_font.get_cmap().size();
		page_font.merge_cmap_from(found->second);
		const size_t after = page_font.get_cmap().size();
		if(after > before)
		  {
		    LOG_S(INFO) << "font " << key << " (" << base << "): adopted "
				<< (after - before) << " /ToUnicode entries from the page registry";
		  }
	      }
	  }

	page_fonts.emplace(key, std::move(page_font));

	double font_time = font_timer.get_time();
	total_font_time += font_time;
	// per-font (dynamic) timing disabled for now; only the total is reported
	//timings.add_timing(pdf_timings::PREFIX_DECODE_FONT + key, font_time);
      }

    share_cmaps_between_subset_siblings();

    timings.add_timing(pdf_timings::KEY_DECODE_FONTS_TOTAL, total_font_time);
    timings.note_attributed(total_font_time);
  }

  std::string pdf_resource<PAGE_FONTS>::base_font_key(std::string name)
  {
    if(not name.empty() and name.front()=='/') { name.erase(0, 1); }
    if(name.size()>7 and name[6]=='+') { name.erase(0, 7); }
    return name;
  }

  void pdf_resource<PAGE_FONTS>::harvest_to_unicode(QPDFObjectHandle resources,
                                                    pdf_timings& timings)
  {
    if(not tounicode_registry_ or not resources.isDictionary())
      {
        return;
      }

    std::set<std::pair<int, int> > visited;

    std::function<void(QPDFObjectHandle)> walk = [&](QPDFObjectHandle res)
    {
      if(not res.isDictionary()) { return; }

      if(res.isIndirect())
        {
          auto og = std::make_pair(res.getObjectID(), res.getGeneration());
          if(not visited.insert(og).second) { return; }
        }

      if(res.hasKey("/Font") and res.getKey("/Font").isDictionary())
        {
          QPDFObjectHandle fonts = res.getKey("/Font");
          for(auto& key : fonts.getKeys())
            {
              QPDFObjectHandle font = fonts.getKey(key);
              if(not font.isDictionary()) { continue; }

              // Sharing is only sound between Identity-H/V fonts, where the
              // code IS the glyph index of the same base font's glyph order.
              std::string enc = "";
              if(font.hasKey("/Encoding") and font.getKey("/Encoding").isName())
                {
                  enc = font.getKey("/Encoding").getName();
                }
              if(enc != "/Identity-H" and enc != "/Identity-V") { continue; }

              if(not (font.hasKey("/BaseFont") and font.getKey("/BaseFont").isName()))
                {
                  continue;
                }
              const std::string base = base_font_key(font.getKey("/BaseFont").getName());
              if(base.empty()) { continue; }

              if(not (font.hasKey("/ToUnicode") and font.getKey("/ToUnicode").isStream()))
                {
                  continue;
                }

              try
                {
                  std::vector<qpdf_stream_instruction> stream;
                  qpdf_stream_decoder decoder(stream);
                  QPDFObjectHandle tu = font.getKey("/ToUnicode");
                  decoder.decode(tu);

                  std::string key_root = "";
                  cmap_parser parser;
                  parser.parse(stream, timings, key_root);
                  cmap_value cv = parser.get();

                  if(cv.is_identity() or cv.empty()) { continue; }

                  auto found = tounicode_registry_->find(base);
                  if(found == tounicode_registry_->end() or found->second.size() < cv.size())
                    {
                      LOG_S(INFO) << "tounicode registry: " << base << " <- " << key
                                  << " (" << cv.size() << " entries)";
                      (*tounicode_registry_)[base] = std::move(cv);
                    }
                }
              catch(const std::exception& e)
                {
                  LOG_S(WARNING) << "tounicode registry: could not parse /ToUnicode of "
                                 << key << ": " << e.what();
                }
            }
        }

      if(res.hasKey("/XObject") and res.getKey("/XObject").isDictionary())
        {
          QPDFObjectHandle xobjs = res.getKey("/XObject");
          for(auto& key : xobjs.getKeys())
            {
              QPDFObjectHandle xo = xobjs.getKey(key);
              if(not xo.isStream()) { continue; }
              QPDFObjectHandle dict = xo.getDict();
              if(dict.hasKey("/Resources"))
                {
                  walk(dict.getKey("/Resources"));
                }
            }
        }
    };

    walk(resources);

    LOG_S(INFO) << "tounicode registry holds " << tounicode_registry_->size()
                << " base font(s)";
  }

  // A document commonly carries the same base font twice: once embedded as a
  // subset with a full /ToUnicode, and once referenced by name with a
  // /ToUnicode covering only ASCII. Both are Identity-H over the same font's
  // glyph order, so a given code denotes the same glyph in both, and the
  // richer table decodes the poorer font's codes correctly.
  //
  // Without this, a code the poorer font cannot map falls through to
  // `utf8::append(code)` -- the raw code emitted as a character. That does not
  // read as corruption: code 0x165C prints as an obscure but perfectly valid
  // "ᙜ" where the page actually says "当", so a Japanese page comes out as
  // fluent-looking mojibake in both the render and the extracted cells.
  //
  // Own entries always win; this only fills gaps.
  void pdf_resource<PAGE_FONTS>::share_cmaps_between_subset_siblings()
  {
    auto base_key = [](std::string name)
    {
      if(not name.empty() and name.front()=='/') { name.erase(0, 1); }
      // Subset prefixes are exactly six uppercase letters followed by '+'.
      if(name.size()>7 and name[6]=='+') { name.erase(0, 7); }
      return name;
    };

    // Richest real (non-identity) map per base font.
    std::unordered_map<std::string, const cmap_value*> donors;
    for(auto& itr : page_fonts)
      {
        const cmap_value& cm = itr.second.get_cmap();
        if(cm.is_identity() or cm.empty()) { continue; }

        const std::string base = base_key(itr.second.get_base_font());
        if(base.empty()) { continue; }

        auto found = donors.find(base);
        if(found==donors.end() or found->second->size() < cm.size())
          {
            donors[base] = &cm;
          }
      }

    if(donors.empty()) { return; }

    for(auto& itr : page_fonts)
      {
        const std::string base = base_key(itr.second.get_base_font());
        auto donor = donors.find(base);
        if(donor==donors.end()) { continue; }
        if(&(itr.second.get_cmap()) == donor->second) { continue; }

        const size_t before = itr.second.get_cmap().size();
        itr.second.merge_cmap_from(*(donor->second));
        const size_t after = itr.second.get_cmap().size();

        if(after > before)
          {
            LOG_S(INFO) << "font " << itr.first << " (" << base << "): adopted "
                        << (after - before) << " /ToUnicode entries from a subset sibling";
          }
      }
  }

}

#endif
