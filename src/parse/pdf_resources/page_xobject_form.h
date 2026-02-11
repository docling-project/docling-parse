//-*-C++-*-

#ifndef PDF_PAGE_XOBJECT_FORM_RESOURCE_H
#define PDF_PAGE_XOBJECT_FORM_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_XOBJECT_FORM>
  {
    inline static const std::vector<std::string> FONTS_KEY = {"/Resources", "/Font"};
    inline static const std::vector<std::string> GRPHS_KEY = {"/Resources", "/ExtGState"};
    inline static const std::vector<std::string> XOBJS_KEY = {"/Resources", "/XObject"};
    
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    std::string          get_key() const;
    xobject_subtype_name get_subtype() const;

    void set(std::string      xobject_key_,
             nlohmann::json&  json_xobject_,
             QPDFObjectHandle qpdf_xobject_);

    // Form-specific API
    std::array<double, 6> get_matrix();
    std::array<double, 4> get_bbox();

    bool has_fonts();
    bool has_grphs();
    bool has_xobjects();
    
    std::pair<nlohmann::json, QPDFObjectHandle> get_fonts();
    std::pair<nlohmann::json, QPDFObjectHandle> get_grphs();
    std::pair<nlohmann::json, QPDFObjectHandle> get_xobjects();

    std::vector<qpdf_instruction> parse_stream();

  private:

    void parse();

    void parse_matrix();

    void parse_bbox();

  private:

    nlohmann::json   json_xobject;
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
    return json_xobject;
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
                                             nlohmann::json&  json_xobject_,
                                             QPDFObjectHandle qpdf_xobject_)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << xobject_key_;

    xobject_key  = xobject_key_;

    json_xobject = json_xobject_;
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
    return utils::json::has(FONTS_KEY, json_xobject_dict);
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_grphs()
  {
    return utils::json::has(GRPHS_KEY, json_xobject_dict);
  }

  bool pdf_resource<PAGE_XOBJECT_FORM>::has_xobjects()
  {
    return utils::json::has(XOBJS_KEY, json_xobject_dict);
  }

  std::pair<nlohmann::json, QPDFObjectHandle> pdf_resource<PAGE_XOBJECT_FORM>::get_fonts()
  {
    std::pair<nlohmann::json, QPDFObjectHandle> fonts;

    if(utils::json::has(FONTS_KEY, json_xobject_dict))
      {
        fonts.first  = utils::json::get(FONTS_KEY, json_xobject_dict);
        fonts.second = qpdf_xobject_dict.getKey(FONTS_KEY[0]).getKey(FONTS_KEY[1]);
      }
    else
      {
        LOG_S(WARNING) << "no '/Font' key detected: " << json_xobject_dict.dump(2);
      }

    return fonts;
  }

  std::pair<nlohmann::json, QPDFObjectHandle> pdf_resource<PAGE_XOBJECT_FORM>::get_grphs()
  {
    std::pair<nlohmann::json, QPDFObjectHandle> grphs;

    if(utils::json::has(GRPHS_KEY, json_xobject_dict))
      {
        grphs.first  = utils::json::get(GRPHS_KEY, json_xobject_dict);
        grphs.second = qpdf_xobject_dict.getKey(GRPHS_KEY[0]).getKey(GRPHS_KEY[1]);
      }
    else
      {
        LOG_S(WARNING) << "no '/ExtGState' key detected: " << json_xobject_dict.dump(2);
      }

    return grphs;
  }

  std::pair<nlohmann::json, QPDFObjectHandle> pdf_resource<PAGE_XOBJECT_FORM>::get_xobjects()
  {
    std::pair<nlohmann::json, QPDFObjectHandle> xobjects;

    if(utils::json::has(XOBJS_KEY, json_xobject_dict))
      {
        xobjects.first  = utils::json::get(XOBJS_KEY, json_xobject_dict);
        xobjects.second = qpdf_xobject_dict.getKey(XOBJS_KEY[0]).getKey(XOBJS_KEY[1]);
      }
    else
      {
        LOG_S(WARNING) << "no '/XObject' key detected";
      }

    return xobjects;
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
