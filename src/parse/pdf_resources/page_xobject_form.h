//-*-C++-*-

#ifndef PDF_PAGE_XOBJECT_FORM_RESOURCE_H
#define PDF_PAGE_XOBJECT_FORM_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_XOBJECT_FORM>
  {
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

    std::pair<nlohmann::json, QPDFObjectHandle> get_fonts();
    std::pair<nlohmann::json, QPDFObjectHandle> get_grphs();
    std::pair<nlohmann::json, QPDFObjectHandle> get_xobjects();

    std::vector<qpdf_instruction> parse_stream();

  private:

    void parse();

    void init_matrix();

    void init_bbox();

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

    init_matrix();
    init_bbox();
  }

  std::array<double, 6> pdf_resource<PAGE_XOBJECT_FORM>::get_matrix()
  {
    return matrix;
  }

  std::array<double, 4> pdf_resource<PAGE_XOBJECT_FORM>::get_bbox()
  {
    return bbox;
  }

  std::pair<nlohmann::json, QPDFObjectHandle> pdf_resource<PAGE_XOBJECT_FORM>::get_fonts()
  {
    std::pair<nlohmann::json, QPDFObjectHandle> fonts;

    std::vector<std::string> keys = {"/Resources", "/Font"};
    if(utils::json::has(keys, json_xobject_dict))
      {
        fonts.first  = utils::json::get(keys, json_xobject_dict);
        fonts.second = qpdf_xobject_dict.getKey(keys[0]).getKey(keys[1]);
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

    std::vector<std::string> keys = {"/Resources", "/ExtGState"};
    if(utils::json::has(keys, json_xobject_dict))
      {
        grphs.first  = utils::json::get(keys, json_xobject_dict);
        grphs.second = qpdf_xobject_dict.getKey(keys[0]).getKey(keys[1]);
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

    std::vector<std::string> keys = {"/Resources", "/XObject"};
    if(utils::json::has(keys, json_xobject_dict))
      {
        xobjects.first  = utils::json::get(keys, json_xobject_dict);
        xobjects.second = qpdf_xobject_dict.getKey(keys[0]).getKey(keys[1]);
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

  void pdf_resource<PAGE_XOBJECT_FORM>::init_matrix()
  {
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
      }
    else
      {
        LOG_S(WARNING) << "no '/Matrix' key detected";
      }
  }

  void pdf_resource<PAGE_XOBJECT_FORM>::init_bbox()
  {
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
      }
    else
      {
        LOG_S(WARNING) << "no '/BBox' key detected";
      }
  }

}

#endif
