//-*-C++-*-

#ifndef PDF_PAGE_XOBJECT_FORM_RESOURCE_H
#define PDF_PAGE_XOBJECT_FORM_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_XOBJECT_FORM>
  {
    const static inline std::string RESOURCES_KEY = "/Resources";
    const static inline std::string FONTS_KEY = "/Font";
    const static inline std::string GRPHS_KEY = "/ExtGState";
    const static inline std::string COLORSPACES_KEY = "/ColorSpace";
    const static inline std::string XOBJS_KEY = "/XObject";
    
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get() const;

    std::string          get_key() const;
    xobject_subtype_name get_subtype() const;

    void set(std::string      xobject_key_,
             QPDFObjectHandle qpdf_xobject_);

    // Form-specific API
    std::array<double, 6> get_matrix() const;
    std::array<double, 4> get_bbox() const;

    bool has_fonts() const;
    bool has_grphs() const;
    bool has_colorspaces() const;
    bool has_xobjects() const;

    QPDFObjectHandle get_fonts() const;
    QPDFObjectHandle get_grphs() const;
    QPDFObjectHandle get_colorspaces() const;
    QPDFObjectHandle get_xobjects() const;

    std::vector<qpdf_stream_instruction> parse_stream() const;

  private:

    void parse();

    void parse_matrix();

    void parse_bbox();

  private:

    QPDFObjectHandle qpdf_xobject;

    QPDFObjectHandle qpdf_xobject_dict;

    std::string xobject_key;

    std::array<double, 6> matrix;
    std::array<double, 4> bbox;
  };

  pdf_resource<PAGE_XOBJECT_FORM>::pdf_resource()
  {}

  pdf_resource<PAGE_XOBJECT_FORM>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECT_FORM>::get() const
  {
    return to_json(qpdf_xobject);
  }

  std::string pdf_resource<PAGE_XOBJECT_FORM>::get_key() const
  {
    return xobject_key;
  }

  xobject_subtype_name pdf_resource<PAGE_XOBJECT_FORM>::get_subtype() const
  {
    return XOBJECT_FORM;
  }

  void pdf_resource<PAGE_XOBJECT_FORM>::set(std::string      xobject_key_,
                                             QPDFObjectHandle qpdf_xobject_)
  {
    // LOG_S(INFO) << __FUNCTION__ << ": " << xobject_key_;

    xobject_key  = xobject_key_;
    qpdf_xobject = qpdf_xobject_;

    parse();
  }

  void pdf_resource<PAGE_XOBJECT_FORM>::parse()
  {
    // LOG_S(INFO) << __FUNCTION__;

    qpdf_xobject_dict = qpdf_xobject.getDict();

    parse_matrix();
    parse_bbox();
  }

  std::array<double, 6> pdf_resource<PAGE_XOBJECT_FORM>::get_matrix() const
  {
    return matrix;
  }

  std::array<double, 4> pdf_resource<PAGE_XOBJECT_FORM>::get_bbox() const
  {
    return bbox;
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_fonts() const
  {
    QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
    return qpdf_xobject_dict_.hasKey(RESOURCES_KEY) and
           qpdf_xobject_dict_.getKey(RESOURCES_KEY).hasKey(FONTS_KEY);
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_grphs() const
  {
    QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
    return qpdf_xobject_dict_.hasKey(RESOURCES_KEY) and
           qpdf_xobject_dict_.getKey(RESOURCES_KEY).hasKey(GRPHS_KEY);
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_colorspaces() const
  {
    QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
    return qpdf_xobject_dict_.hasKey(RESOURCES_KEY) and
           qpdf_xobject_dict_.getKey(RESOURCES_KEY).hasKey(COLORSPACES_KEY);
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_xobjects() const
  {
    QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
    return qpdf_xobject_dict_.hasKey(RESOURCES_KEY) and
           qpdf_xobject_dict_.getKey(RESOURCES_KEY).hasKey(XOBJS_KEY);
  }

  QPDFObjectHandle pdf_resource<PAGE_XOBJECT_FORM>::get_fonts() const
  {
    if(has_fonts())
      {
        QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
	return qpdf_xobject_dict_.getKey(RESOURCES_KEY).getKey(FONTS_KEY);
      }

    // LOG_S(WARNING) << "no '/Font' key detected in xobject dict";
    return QPDFObjectHandle::newNull();
  }

  QPDFObjectHandle pdf_resource<PAGE_XOBJECT_FORM>::get_grphs() const
  {
    if(has_grphs())
      {
        QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
	return qpdf_xobject_dict_.getKey(RESOURCES_KEY).getKey(GRPHS_KEY);
      }

    // LOG_S(WARNING) << "no '/ExtGState' key detected in xobject dict";
    return QPDFObjectHandle::newNull();
  }

  QPDFObjectHandle pdf_resource<PAGE_XOBJECT_FORM>::get_colorspaces() const
  {
    if(has_colorspaces())
      {
        QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
	return qpdf_xobject_dict_.getKey(RESOURCES_KEY).getKey(COLORSPACES_KEY);
      }

    return QPDFObjectHandle::newNull();
  }

  QPDFObjectHandle pdf_resource<PAGE_XOBJECT_FORM>::get_xobjects() const
  {
    if(has_xobjects())
      {
        QPDFObjectHandle qpdf_xobject_dict_ = qpdf_xobject_dict;
	return qpdf_xobject_dict_.getKey(RESOURCES_KEY).getKey(XOBJS_KEY);
      }

    // LOG_S(WARNING) << "no '/XObject' key detected in xobject dict";
    return QPDFObjectHandle::newNull();
  }

  std::vector<qpdf_stream_instruction> pdf_resource<PAGE_XOBJECT_FORM>::parse_stream() const
  {
    std::vector<qpdf_stream_instruction> stream;

    // decode the stream
    try
      {
        qpdf_stream_decoder decoder(stream);
        QPDFObjectHandle qpdf_xobject_ = qpdf_xobject;
        decoder.decode(qpdf_xobject_);

        decoder.print();
      }
    catch(const std::exception& exc)
      {
        std::stringstream ss;
        ss << "encountered an error: " << exc.what();
        LOG_S(ERROR) << ss.str();
	
        throw std::logic_error(ss.str());
      }

    return stream;
  }

  void pdf_resource<PAGE_XOBJECT_FORM>::parse_matrix()
  {
    // LOG_S(INFO) << __FUNCTION__;
    
    matrix = {1., 0., 0., 1., 0., 0.};

    // Read the '/Matrix' array directly off the qpdf dict instead of going
    // through a full recursive to_json of the xobject dict.
    if(qpdf_xobject_dict.hasKey("/Matrix") and qpdf_xobject_dict.getKey("/Matrix").isArray())
      {
        QPDFObjectHandle qpdf_matrix = qpdf_xobject_dict.getKey("/Matrix");

        if(matrix.size()!=qpdf_matrix.getArrayNItems())
          {
            std::string message = "matrix.size()!=qpdf_matrix.getArrayNItems()";
	    LOG_S(ERROR) << message;
            throw std::logic_error(message);
          }

        for(int l=0; l<matrix.size(); l++)
          {
            QPDFObjectHandle num = qpdf_matrix.getArrayItem(l);
            if(num.isNumber())
              {
                matrix[l] = utils::numeric::locale_safe_numeric_value(num);
              }
            else
              {
                //LOG_S(WARNING) << "'/Matrix'[" << l << "] is not a number (type: "
		//<< num.getTypeName() << "), keeping identity default "
		//<< matrix[l];
              }
          }

	LOG_S(INFO) << "matrix: ["
		    << matrix.at(0) << ", "
		    << matrix.at(1) << ", "
		    << matrix.at(2) << ", "
		    << matrix.at(3) << ", "
		    << matrix.at(4) << ", "
		    << matrix.at(5) << "]";	
      }
    else
      {
        // LOG_S(WARNING) << "no '/Matrix' key detected";
      }
  }

  void pdf_resource<PAGE_XOBJECT_FORM>::parse_bbox()
  {
    // LOG_S(INFO) << __FUNCTION__;
    
    bbox = {0., 0., 0., 0.};

    // Read the '/BBox' array directly off the qpdf dict instead of going
    // through a full recursive to_json of the xobject dict.
    if(qpdf_xobject_dict.hasKey("/BBox") and qpdf_xobject_dict.getKey("/BBox").isArray())
      {
        QPDFObjectHandle qpdf_bbox = qpdf_xobject_dict.getKey("/BBox");

        if(bbox.size()!=qpdf_bbox.getArrayNItems())
          {
            std::string message = "bbox.size()!=qpdf_bbox.getArrayNItems()";
            LOG_S(ERROR) << message;
            throw std::logic_error(message);
          }

        for(int l=0; l<bbox.size(); l++)
          {
            QPDFObjectHandle num = qpdf_bbox.getArrayItem(l);
            if(num.isNumber())
              {
                bbox[l] = utils::numeric::locale_safe_numeric_value(num);
              }
            else
              {
                LOG_S(ERROR) << "'/BBox'[" << l << "] is not a number (type: "
                             << num.getTypeName() << "), keeping default "
                             << bbox[l] << " (bbox is required!)";
              }
          }

	    //LOG_S(INFO) << "bbox: ["
	    // << bbox.at(0) << ", "
	    // << bbox.at(1) << ", "
	    // << bbox.at(2) << ", "
	    // << bbox.at(3) << "]";		
      }
    else
      {
	LOG_S(ERROR) << "no '/BBox' key detected and it is required!";
      }
  }

}

#endif
