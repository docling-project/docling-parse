//-*-C++-*-

#ifndef PDF_PAGE_GRPH_RESOURCE_H
#define PDF_PAGE_GRPH_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_GRPH>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    void set(const std::string& key_,
	     QPDFObjectHandle qpdf_grph);

    // /CA (stroking) and /ca (non-stroking) constant alpha; a `gs` operator
    // only changes the parameters present in its dictionary, hence the
    // has_* flags. 1.0 = opaque, 0.0 = fully transparent.
    bool has_stroke_alpha() const { return has_stroke_alpha_; }
    bool has_fill_alpha()   const { return has_fill_alpha_; }

    double get_stroke_alpha() const { return stroke_alpha_; }
    double get_fill_alpha()   const { return fill_alpha_; }

  private:

    std::string    key;
    nlohmann::json val;

    bool   has_stroke_alpha_ = false;
    bool   has_fill_alpha_   = false;
    double stroke_alpha_     = 1.0;
    double fill_alpha_       = 1.0;
  };

  pdf_resource<PAGE_GRPH>::pdf_resource():
    key(""),
    val(nullptr)
  {}

  pdf_resource<PAGE_GRPH>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_GRPH>::get()
  {
    return val;
  }

  void pdf_resource<PAGE_GRPH>::set(const std::string& key_,
				    QPDFObjectHandle qpdf_grph)
  {
    key = key_;

    // only the parameters needed downstream are extracted; the full
    // ExtGState dictionary (SMask, BM, ...) stays unparsed
    if(qpdf_grph.isDictionary())
      {
	if(qpdf_grph.hasKey("/CA") and qpdf_grph.getKey("/CA").isNumber())
	  {
	    QPDFObjectHandle CA = qpdf_grph.getKey("/CA");

	    has_stroke_alpha_ = true;
	    stroke_alpha_ = utils::numeric::locale_safe_numeric_value(CA);
	  }

	if(qpdf_grph.hasKey("/ca") and qpdf_grph.getKey("/ca").isNumber())
	  {
	    QPDFObjectHandle ca = qpdf_grph.getKey("/ca");

	    has_fill_alpha_ = true;
	    fill_alpha_ = utils::numeric::locale_safe_numeric_value(ca);
	  }
      }
  }

}

#endif
