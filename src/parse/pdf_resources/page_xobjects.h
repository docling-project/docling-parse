//-*-C++-*-

#ifndef PDF_PAGE_XOBJECTS_RESOURCE_H
#define PDF_PAGE_XOBJECTS_RESOURCE_H

namespace pdflib
{

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

    pdf_resource<PAGE_XOBJECT_IMAGE>&      get_image(std::string name);
    pdf_resource<PAGE_XOBJECT_FORM>&       get_form(std::string name);
    pdf_resource<PAGE_XOBJECT_POSTSCRIPT>& get_postscript(std::string name);

    void set(nlohmann::json&   json_xobjects,
             QPDFObjectHandle& qpdf_xobjects_,
             pdf_timings& timings);

  private:

    static xobject_subtype_name detect_subtype(QPDFObjectHandle& qpdf_obj);

    std::shared_ptr<pdf_resource<PAGE_XOBJECTS>> parent_;
    
    std::unordered_map<std::string, pdf_resource<PAGE_XOBJECT_IMAGE> >      image_xobjects;
    std::unordered_map<std::string, pdf_resource<PAGE_XOBJECT_FORM> >       form_xobjects;
    std::unordered_map<std::string, pdf_resource<PAGE_XOBJECT_POSTSCRIPT> > postscript_xobjects;
  };

  pdf_resource<PAGE_XOBJECTS>::pdf_resource():
    parent_(nullptr)
  {}

  pdf_resource<PAGE_XOBJECTS>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_XOBJECTS>> parent):
    parent_(parent)
  {}

  pdf_resource<PAGE_XOBJECTS>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECTS>::get()
  {
    nlohmann::json result;

    for(auto& itr : image_xobjects)
      {
        result[itr.first] = itr.second.get();
      }

    for(auto& itr : form_xobjects)
      {
        result[itr.first] = itr.second.get();
      }

    for(auto& itr : postscript_xobjects)
      {
        result[itr.first] = itr.second.get();
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

  pdf_resource<PAGE_XOBJECT_IMAGE>& pdf_resource<PAGE_XOBJECTS>::get_image(std::string name)
  {
    if(image_xobjects.count(name)==1)
      {
        return image_xobjects.at(name);
      }
    if(parent_)
      {
        return parent_->get_image(name);
      }
    std::string message = "image_xobjects does not contain: " + name;
    LOG_S(ERROR) << message;
    throw std::logic_error(message);
  }

  pdf_resource<PAGE_XOBJECT_FORM>& pdf_resource<PAGE_XOBJECTS>::get_form(std::string name)
  {
    if(form_xobjects.count(name)==1)
      {
        return form_xobjects.at(name);
      }
    if(parent_)
      {
        return parent_->get_form(name);
      }
    std::string message = "form_xobjects does not contain: " + name;
    LOG_S(ERROR) << message;
    throw std::logic_error(message);
  }

  pdf_resource<PAGE_XOBJECT_POSTSCRIPT>& pdf_resource<PAGE_XOBJECTS>::get_postscript(std::string name)
  {
    if(postscript_xobjects.count(name)==1)
      {
        return postscript_xobjects.at(name);
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
    QPDFObjectHandle dict = qpdf_obj.getDict();
    nlohmann::json json_dict = to_json(dict);

    if(json_dict.count("/Subtype"))
      {
        std::string subtype = json_dict["/Subtype"].get<std::string>();

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
      }

    LOG_S(ERROR) << "unknown XObject subtype";
    return XOBJECT_UNKNOWN;
  }

  void pdf_resource<PAGE_XOBJECTS>::set(nlohmann::json&   json_xobjects,
                                        QPDFObjectHandle& qpdf_xobjects,
                                        pdf_timings& timings)
  {
    LOG_S(INFO) << __FUNCTION__;

    double total_xobject_time = 0.0;

    int cnt = 0;
    int len = json_xobjects.size();

    for(auto& pair : json_xobjects.items())
      {
        LOG_S(INFO) << "decoding xobject: " << pair.key() << "\t" << (++cnt) << "/" << len;

        std::string key = pair.key();
	nlohmann::json& val = pair.value();

	QPDFObjectHandle qpdf_obj = qpdf_xobjects.getKey(key);
	xobject_subtype_name subtype = detect_subtype(qpdf_obj);

	utils::timer xobject_timer;

	switch(subtype)
	  {
	  case XOBJECT_IMAGE:
	    {
	      if(image_xobjects.count(key)>0)
		{
		  LOG_S(ERROR) << key << " is already in image_xobjects, overwriting ...";
		}

	      pdf_resource<PAGE_XOBJECT_IMAGE> xobj;
	      xobj.set(key, val, qpdf_obj);
	      image_xobjects[key] = xobj;
	    }
	    break;

	  case XOBJECT_FORM:
	    {
	      if(form_xobjects.count(key)>0)
		{
		  LOG_S(ERROR) << key << " is already in form_xobjects, overwriting ...";
		}

	      pdf_resource<PAGE_XOBJECT_FORM> xobj;
	      xobj.set(key, val, qpdf_obj);
	      form_xobjects[key] = xobj;
	    }
	    break;

	  default:
	    {
	      if(postscript_xobjects.count(key)>0)
		{
		  LOG_S(ERROR) << key << " is already in postscript_xobjects, overwriting ...";
		}

	      pdf_resource<PAGE_XOBJECT_POSTSCRIPT> xobj;
	      xobj.set(key, val, qpdf_obj);
	      postscript_xobjects[key] = xobj;
	    }
	    break;
	  }

	double xobject_time = xobject_timer.get_time();
	total_xobject_time += xobject_time;
	timings.add_timing(pdf_timings::PREFIX_DECODE_XOBJECT + key, xobject_time);
      }

    timings.add_timing(pdf_timings::KEY_DECODE_XOBJECTS_TOTAL, total_xobject_time);
  }

}

#endif
