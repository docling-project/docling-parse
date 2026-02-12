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
    const static inline std::string XOBJS_KEY = "/XObject";
    
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    std::string          get_key() const;
    xobject_subtype_name get_subtype() const;

    void set(std::string      xobject_key_,
             QPDFObjectHandle qpdf_xobject_);

    // Form-specific API
    std::array<double, 6> get_matrix();
    std::array<double, 4> get_bbox();

    bool has_fonts();
    bool has_grphs();
    bool has_xobjects();

    QPDFObjectHandle get_fonts();
    QPDFObjectHandle get_grphs();
    QPDFObjectHandle get_xobjects();

    std::vector<qpdf_instruction> parse_stream();

  private:

    void parse();

    void parse_matrix();

    void parse_bbox();

  private:

    QPDFObjectHandle qpdf_xobject;

    QPDFObjectHandle qpdf_xobject_dict;
    nlohmann::json   json_xobject_dict;

    std::string xobject_key;

    std::array<double, 6> matrix;
    std::array<double, 4> bbox;
  };

  pdf_resource<PAGE_XOBJECT_FORM>::pdf_resource()
  {}

  pdf_resource<PAGE_XOBJECT_FORM>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECT_FORM>::get()
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
    LOG_S(INFO) << __FUNCTION__ << ": " << xobject_key_;

    xobject_key  = xobject_key_;
    qpdf_xobject = qpdf_xobject_;

    parse();
  }

  void pdf_resource<PAGE_XOBJECT_FORM>::parse()
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      qpdf_xobject_dict = qpdf_xobject.getDict();
      json_xobject_dict = to_json(qpdf_xobject_dict);
    }

    parse_matrix();
    parse_bbox();
  }

  std::array<double, 6> pdf_resource<PAGE_XOBJECT_FORM>::get_matrix()
  {
    return matrix;
  }

  std::array<double, 4> pdf_resource<PAGE_XOBJECT_FORM>::get_bbox()
  {
    return bbox;
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_fonts()
  {
    return qpdf_xobject_dict.hasKey(RESOURCES_KEY) and
           qpdf_xobject_dict.getKey(RESOURCES_KEY).hasKey(FONTS_KEY);
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_grphs()
  {
    return qpdf_xobject_dict.hasKey(RESOURCES_KEY) and
           qpdf_xobject_dict.getKey(RESOURCES_KEY).hasKey(GRPHS_KEY);
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_xobjects()
  {
    return qpdf_xobject_dict.hasKey(RESOURCES_KEY) and
           qpdf_xobject_dict.getKey(RESOURCES_KEY).hasKey(XOBJS_KEY);
  }

  QPDFObjectHandle pdf_resource<PAGE_XOBJECT_FORM>::get_fonts()
  {
    if(has_fonts())
      {
	return qpdf_xobject_dict.getKey(RESOURCES_KEY).getKey(FONTS_KEY);
      }

    LOG_S(WARNING) << "no '/Font' key detected in xobject dict";
    return QPDFObjectHandle::newNull();
  }

  QPDFObjectHandle pdf_resource<PAGE_XOBJECT_FORM>::get_grphs()
  {
    if(has_grphs())
      {
	return qpdf_xobject_dict.getKey(RESOURCES_KEY).getKey(GRPHS_KEY);
      }

    LOG_S(WARNING) << "no '/ExtGState' key detected in xobject dict";
    return QPDFObjectHandle::newNull();
  }

  QPDFObjectHandle pdf_resource<PAGE_XOBJECT_FORM>::get_xobjects()
  {
    if(has_xobjects())
      {
	return qpdf_xobject_dict.getKey(RESOURCES_KEY).getKey(XOBJS_KEY);
      }

    LOG_S(WARNING) << "no '/XObject' key detected in xobject dict";
    return QPDFObjectHandle::newNull();
  }

  std::vector<qpdf_instruction> pdf_resource<PAGE_XOBJECT_FORM>::parse_stream()
  {
    std::vector<qpdf_instruction> stream;

    // decode the stream
    try
      {
        qpdf_stream_decoder decoder(stream);
        decoder.decode(qpdf_xobject);

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
    LOG_S(INFO) << __FUNCTION__;
    
    matrix = {1., 0., 0., 1., 0., 0.};

    std::vector<std::string> keys = {"/Matrix"};
    if(utils::json::has(keys, json_xobject_dict))
      {
        nlohmann::json json_matrix = utils::json::get(keys, json_xobject_dict);

        if(matrix.size()!=json_matrix.size())
          {
            std::string message = "matrix.size()!=json_matrix.size()";
            LOG_S(ERROR) << message;
            throw std::logic_error(message);
          }

        for(int l=0; l<matrix.size(); l++)
          {
            matrix[l] = json_matrix[l].get<double>();
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
        LOG_S(WARNING) << "no '/Matrix' key detected";
      }
  }

  void pdf_resource<PAGE_XOBJECT_FORM>::parse_bbox()
  {
    LOG_S(INFO) << __FUNCTION__;
    
    bbox = {0., 0., 0., 0.};

    std::vector<std::string> keys = {"/BBox"};
    if(utils::json::has(keys, json_xobject_dict))
      {
        nlohmann::json json_bbox = utils::json::get(keys, json_xobject_dict);

        if(bbox.size()!=json_bbox.size())
          {
            std::string message = "bbox.size()!=json_bbox.size()";
            LOG_S(ERROR) << message;
            throw std::logic_error(message);
          }

        for(int l=0; l<bbox.size(); l++)
          {
            bbox[l] = json_bbox[l].get<double>();
          }

	LOG_S(INFO) << "bbox: ["
		    << bbox.at(0) << ", "
		    << bbox.at(1) << ", "
		    << bbox.at(2) << ", "
		    << bbox.at(3) << "]";		
      }
    else
      {
        LOG_S(ERROR) << "no '/BBox' key detected and it is required!";
      }
  }

}

#endif
