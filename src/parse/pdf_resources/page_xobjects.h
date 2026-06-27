//-*-C++-*-

#ifndef PDF_PAGE_XOBJECTS_RESOURCE_H
#define PDF_PAGE_XOBJECTS_RESOURCE_H

namespace pdflib
{
  class qpdf_objgen_hash
  {
  public:

    std::size_t operator()(QPDFObjGen const& og) const;
  };

  class xobject_parse_cache
  {
  public:

    bool find_subtype(QPDFObjGen const& og,
                      xobject_subtype_name& subtype) const;

    void store_subtype(QPDFObjGen const& og,
                       xobject_subtype_name subtype);

    std::shared_ptr<const pdf_resource<PAGE_XOBJECT_IMAGE> > find_image(QPDFObjGen const& og) const;

    void store_image(QPDFObjGen const& og,
                     std::shared_ptr<const pdf_resource<PAGE_XOBJECT_IMAGE> > xobj);

    std::shared_ptr<const pdf_resource<PAGE_XOBJECT_FORM> > find_form(QPDFObjGen const& og) const;

    void store_form(QPDFObjGen const& og,
                    std::shared_ptr<const pdf_resource<PAGE_XOBJECT_FORM> > xobj);

    std::shared_ptr<const pdf_resource<PAGE_XOBJECT_POSTSCRIPT> > find_postscript(QPDFObjGen const& og) const;

    void store_postscript(QPDFObjGen const& og,
                          std::shared_ptr<const pdf_resource<PAGE_XOBJECT_POSTSCRIPT> > xobj);

  private:

    std::unordered_map<QPDFObjGen,
                       xobject_subtype_name,
                       qpdf_objgen_hash> subtype_;
    std::unordered_map<QPDFObjGen,
                       std::shared_ptr<const pdf_resource<PAGE_XOBJECT_IMAGE> >,
                       qpdf_objgen_hash> image_;
    std::unordered_map<QPDFObjGen,
                       std::shared_ptr<const pdf_resource<PAGE_XOBJECT_FORM> >,
                       qpdf_objgen_hash> form_;
    std::unordered_map<QPDFObjGen,
                       std::shared_ptr<const pdf_resource<PAGE_XOBJECT_POSTSCRIPT> >,
                       qpdf_objgen_hash> postscript_;
  };

  inline std::size_t qpdf_objgen_hash::operator()(QPDFObjGen const& og) const
  {
    std::size_t h1 = std::hash<int>{}(og.getObj());
    std::size_t h2 = std::hash<int>{}(og.getGen());
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
  }

  inline bool xobject_parse_cache::find_subtype(QPDFObjGen const& og,
                                                xobject_subtype_name& subtype) const
  {
    auto itr = subtype_.find(og);
    if(itr==subtype_.end())
      {
        return false;
      }

    subtype = itr->second;
    return true;
  }

  inline void xobject_parse_cache::store_subtype(QPDFObjGen const& og,
                                                 xobject_subtype_name subtype)
  {
    subtype_[og] = subtype;
  }

  inline std::shared_ptr<const pdf_resource<PAGE_XOBJECT_IMAGE> >
  xobject_parse_cache::find_image(QPDFObjGen const& og) const
  {
    auto itr = image_.find(og);
    if(itr==image_.end())
      {
        return nullptr;
      }

    return itr->second;
  }

  inline void
  xobject_parse_cache::store_image(QPDFObjGen const& og,
                                   std::shared_ptr<const pdf_resource<PAGE_XOBJECT_IMAGE> > xobj)
  {
    image_[og] = xobj;
  }

  inline std::shared_ptr<const pdf_resource<PAGE_XOBJECT_FORM> >
  xobject_parse_cache::find_form(QPDFObjGen const& og) const
  {
    auto itr = form_.find(og);
    if(itr==form_.end())
      {
        return nullptr;
      }

    return itr->second;
  }

  inline void
  xobject_parse_cache::store_form(QPDFObjGen const& og,
                                  std::shared_ptr<const pdf_resource<PAGE_XOBJECT_FORM> > xobj)
  {
    form_[og] = xobj;
  }

  inline std::shared_ptr<const pdf_resource<PAGE_XOBJECT_POSTSCRIPT> >
  xobject_parse_cache::find_postscript(QPDFObjGen const& og) const
  {
    auto itr = postscript_.find(og);
    if(itr==postscript_.end())
      {
        return nullptr;
      }

    return itr->second;
  }

  inline void
  xobject_parse_cache::store_postscript(QPDFObjGen const& og,
                                        std::shared_ptr<const pdf_resource<PAGE_XOBJECT_POSTSCRIPT> > xobj)
  {
    postscript_[og] = xobj;
  }

  template<>
  class pdf_resource<PAGE_XOBJECTS>
  {
  public:

    pdf_resource();
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_XOBJECTS>> parent);
    ~pdf_resource();

    nlohmann::json get();

    bool has(std::string name);

    xobject_subtype_name get_subtype(std::string name);

    const pdf_resource<PAGE_XOBJECT_IMAGE>&      get_image(std::string name);
    const pdf_resource<PAGE_XOBJECT_FORM>&       get_form(std::string name);
    const pdf_resource<PAGE_XOBJECT_POSTSCRIPT>& get_postscript(std::string name);

    void set(QPDFObjectHandle& qpdf_xobjects_,
             pdf_timings& timings);

  private:

    static xobject_subtype_name detect_subtype(QPDFObjectHandle& qpdf_obj);

    std::shared_ptr<pdf_resource<PAGE_XOBJECTS>> parent_;
    std::shared_ptr<xobject_parse_cache> parse_cache_;
    
    std::unordered_map<std::string, std::shared_ptr<const pdf_resource<PAGE_XOBJECT_IMAGE> > >      image_xobjects;
    std::unordered_map<std::string, std::shared_ptr<const pdf_resource<PAGE_XOBJECT_FORM> > >       form_xobjects;
    std::unordered_map<std::string, std::shared_ptr<const pdf_resource<PAGE_XOBJECT_POSTSCRIPT> > > postscript_xobjects;
  };

  pdf_resource<PAGE_XOBJECTS>::pdf_resource():
    parent_(nullptr),
    parse_cache_(std::make_shared<xobject_parse_cache>())
  {}

  pdf_resource<PAGE_XOBJECTS>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_XOBJECTS>> parent):
    parent_(parent),
    parse_cache_(parent ? parent->parse_cache_ : std::make_shared<xobject_parse_cache>())
  {}

  pdf_resource<PAGE_XOBJECTS>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECTS>::get()
  {
    nlohmann::json result;

    for(auto& itr : image_xobjects)
      {
        result[itr.first] = itr.second->get();
      }

    for(auto& itr : form_xobjects)
      {
        result[itr.first] = itr.second->get();
      }

    for(auto& itr : postscript_xobjects)
      {
        result[itr.first] = itr.second->get();
      }

    return result;
  }

  bool pdf_resource<PAGE_XOBJECTS>::has(std::string name)
  {
    if(image_xobjects.count(name)==1 or
       form_xobjects.count(name)==1 or
       postscript_xobjects.count(name)==1)
      {
        return true;
      }
    if(parent_)
      {
        return parent_->has(name);
      }
    return false;
  }

  xobject_subtype_name pdf_resource<PAGE_XOBJECTS>::get_subtype(std::string name)
  {
    if(image_xobjects.count(name)==1)
      {
        return XOBJECT_IMAGE;
      }
    else if(form_xobjects.count(name)==1)
      {
        return XOBJECT_FORM;
      }
    else if(postscript_xobjects.count(name)==1)
      {
        return XOBJECT_POSTSCRIPT;
      }

    if(parent_)
      {
        return parent_->get_subtype(name);
      }

    LOG_S(ERROR) << "unknown xobject: " << name;
    return XOBJECT_UNKNOWN;
  }

  const pdf_resource<PAGE_XOBJECT_IMAGE>& pdf_resource<PAGE_XOBJECTS>::get_image(std::string name)
  {
    if(image_xobjects.count(name)==1)
      {
        return *image_xobjects.at(name);
      }
    if(parent_)
      {
        return parent_->get_image(name);
      }
    std::string message = "image_xobjects does not contain: " + name;
    LOG_S(ERROR) << message;
    throw std::logic_error(message);
  }

  const pdf_resource<PAGE_XOBJECT_FORM>& pdf_resource<PAGE_XOBJECTS>::get_form(std::string name)
  {
    if(form_xobjects.count(name)==1)
      {
        return *form_xobjects.at(name);
      }
    if(parent_)
      {
        return parent_->get_form(name);
      }
    std::string message = "form_xobjects does not contain: " + name;
    LOG_S(ERROR) << message;
    throw std::logic_error(message);
  }

  const pdf_resource<PAGE_XOBJECT_POSTSCRIPT>& pdf_resource<PAGE_XOBJECTS>::get_postscript(std::string name)
  {
    if(postscript_xobjects.count(name)==1)
      {
        return *postscript_xobjects.at(name);
      }
    if(parent_)
      {
        return parent_->get_postscript(name);
      }
    std::string message = "postscript_xobjects does not contain: " + name;
    LOG_S(ERROR) << message;
    throw std::logic_error(message);
  }

  xobject_subtype_name pdf_resource<PAGE_XOBJECTS>::detect_subtype(QPDFObjectHandle& qpdf_obj)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << qpdf_obj.getTypeName();
    
    if(not qpdf_obj.isStream())
      {
	LOG_S(ERROR) << "qpdf-obj is of type " << qpdf_obj.getTypeName() << " and not a stream";
	return XOBJECT_UNKNOWN;
      }
    
    QPDFObjectHandle dict = qpdf_obj.getDict(); // only works on a stream!

    // Read only the '/Subtype' key directly instead of recursively serialising
    // the whole xobject dict (including nested /Resources) into json.
    std::string subtype = "";
    if(dict.hasKey("/Subtype") and dict.getKey("/Subtype").isName())
      {
        subtype = dict.getKey("/Subtype").getName();
      }
    else if(dict.hasKey("/Subtype") and dict.getKey("/Subtype").isString())
      {
        subtype = dict.getKey("/Subtype").getUTF8Value();
      }

    if(subtype=="/Image")
      {
        return XOBJECT_IMAGE;
      }
    else if(subtype=="/Form")
      {
        return XOBJECT_FORM;
      }
    else if(subtype=="/PS")
      {
        return XOBJECT_POSTSCRIPT;
      }

    LOG_S(ERROR) << "unknown XObject subtype";
    return XOBJECT_UNKNOWN;
  }

  void pdf_resource<PAGE_XOBJECTS>::set(QPDFObjectHandle& qpdf_xobjects,
                                        pdf_timings& timings)
  {
    LOG_S(INFO) << __FUNCTION__;

    //{
    //auto tmp = to_json(qpdf_xobjects); 
    //LOG_S(INFO) << tmp.dump(2);
    //}
    
    double total_xobject_time = 0.0;

    auto keys = qpdf_xobjects.getKeys();
    int cnt = 0;
    int len = keys.size();

    for(auto& key : keys)
      {
        LOG_S(INFO) << "decoding xobject: " << key << "\t" << (++cnt) << "/" << len;

	QPDFObjectHandle qpdf_obj = qpdf_xobjects.getKey(key);
        const bool cacheable = qpdf_obj.isIndirect();
        QPDFObjGen og;
        if(cacheable)
          {
            og = qpdf_obj.getObjGen();
          }

	xobject_subtype_name subtype = XOBJECT_UNKNOWN;
        if(not(cacheable and parse_cache_->find_subtype(og, subtype)))
          {
	    subtype = detect_subtype(qpdf_obj);
            if(cacheable)
              {
                parse_cache_->store_subtype(og, subtype);
              }
          }
	
	utils::timer xobject_timer;

	switch(subtype)
	  {
	  case XOBJECT_IMAGE:
	    {
	      if(image_xobjects.count(key)>0)
		{
		  LOG_S(ERROR) << key << " is already in image_xobjects, overwriting ...";
		}

	      std::shared_ptr<const pdf_resource<PAGE_XOBJECT_IMAGE> > xobj =
                cacheable ? parse_cache_->find_image(og) : nullptr;

              if(not xobj)
                {
                  auto parsed_xobj = std::make_shared<pdf_resource<PAGE_XOBJECT_IMAGE>>();
                  parsed_xobj->set(key, qpdf_obj);
                  xobj = parsed_xobj;

                  if(cacheable)
                    {
                      parse_cache_->store_image(og, xobj);
                    }
                }

	      //to be commented out!!	      
	      //{
	      //std::string fname = "./pic_" + std::to_string(image_xobjects.size()) + xobj.pick_extension();
	      //LOG_S(ERROR) << "storing pic at: " << fname;
		
	      //std::filesystem::path path(fname.c_str());
	      //xobj.save_to_file(path);
	      //}
	      
	      image_xobjects[key] = xobj;		      
	    }
	    break;

	  case XOBJECT_FORM:
	    {
	      if(form_xobjects.count(key)>0)
		{
		  LOG_S(ERROR) << key << " is already in form_xobjects, overwriting ...";
		}

	      std::shared_ptr<const pdf_resource<PAGE_XOBJECT_FORM> > xobj =
                cacheable ? parse_cache_->find_form(og) : nullptr;

              if(not xobj)
                {
                  auto parsed_xobj = std::make_shared<pdf_resource<PAGE_XOBJECT_FORM>>();
                  parsed_xobj->set(key, qpdf_obj);
                  xobj = parsed_xobj;

                  if(cacheable)
                    {
                      parse_cache_->store_form(og, xobj);
                    }
                }

	      form_xobjects[key] = xobj;
	    }
	    break;

	  case XOBJECT_POSTSCRIPT:
	    {
	      if(postscript_xobjects.count(key)>0)
		{
		  LOG_S(ERROR) << key << " is already in postscript_xobjects, overwriting ...";
		}

	      std::shared_ptr<const pdf_resource<PAGE_XOBJECT_POSTSCRIPT> > xobj =
                cacheable ? parse_cache_->find_postscript(og) : nullptr;

              if(not xobj)
                {
                  auto parsed_xobj = std::make_shared<pdf_resource<PAGE_XOBJECT_POSTSCRIPT>>();
                  parsed_xobj->set(key, qpdf_obj);
                  xobj = parsed_xobj;

                  if(cacheable)
                    {
                      parse_cache_->store_postscript(og, xobj);
                    }
                }

	      postscript_xobjects[key] = xobj;
	    }
	    break;

	  default:
	    {
	      auto tmp = to_json(qpdf_obj); 
	      LOG_S(ERROR) << "could not decode xobject: \n" << tmp.dump(2);
	    }
	  }

	double xobject_time = xobject_timer.get_time();
	total_xobject_time += xobject_time;
	// per-xobject (dynamic) timing disabled for now; only the total is reported
	//timings.add_timing(pdf_timings::PREFIX_DECODE_XOBJECT + key, xobject_time);
      }

    timings.add_timing(pdf_timings::KEY_DECODE_XOBJECTS_TOTAL, total_xobject_time);
    timings.note_attributed(total_xobject_time);
  }

}

#endif
