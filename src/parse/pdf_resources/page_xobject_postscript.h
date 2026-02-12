//-*-C++-*-

#ifndef PDF_PAGE_XOBJECT_POSTSCRIPT_RESOURCE_H
#define PDF_PAGE_XOBJECT_POSTSCRIPT_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_XOBJECT_POSTSCRIPT>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    std::string          get_key() const;
    xobject_subtype_name get_subtype() const;

    void set(std::string      xobject_key_,
             QPDFObjectHandle qpdf_xobject_);

  private:

    void parse();

  private:

    QPDFObjectHandle qpdf_xobject;

    QPDFObjectHandle qpdf_xobject_dict;
    nlohmann::json   json_xobject_dict;

    std::string xobject_key;
  };

  pdf_resource<PAGE_XOBJECT_POSTSCRIPT>::pdf_resource()
  {}

  pdf_resource<PAGE_XOBJECT_POSTSCRIPT>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECT_POSTSCRIPT>::get()
  {
    return to_json(qpdf_xobject);
  }

  std::string pdf_resource<PAGE_XOBJECT_POSTSCRIPT>::get_key() const
  {
    return xobject_key;
  }

  xobject_subtype_name pdf_resource<PAGE_XOBJECT_POSTSCRIPT>::get_subtype() const
  {
    return XOBJECT_POSTSCRIPT;
  }

  void pdf_resource<PAGE_XOBJECT_POSTSCRIPT>::set(std::string      xobject_key_,
                                                   QPDFObjectHandle qpdf_xobject_)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << xobject_key_;
    LOG_S(WARNING) << "PostScript XObjects are not fully supported";

    xobject_key  = xobject_key_;
    qpdf_xobject = qpdf_xobject_;

    parse();
  }

  void pdf_resource<PAGE_XOBJECT_POSTSCRIPT>::parse()
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      qpdf_xobject_dict = qpdf_xobject.getDict();
      json_xobject_dict = to_json(qpdf_xobject_dict);
    }
  }

}

#endif
