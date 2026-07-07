//-*-C++-*-

#ifndef PDF_PAGE_COLORSPACES_RESOURCE_H
#define PDF_PAGE_COLORSPACES_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_COLORSPACES>
  {
  public:

    pdf_resource();
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> parent);
    ~pdf_resource();

    size_t size();

    int count(std::string key);

    std::unordered_set<std::string> keys();

    pdf_resource<PAGE_COLORSPACE>& operator[](std::string cs_name);

    void set(QPDFObjectHandle& qpdf_colorspaces);

  private:

    std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> parent_;
    std::unordered_map<std::string, pdf_resource<PAGE_COLORSPACE> > page_colorspaces;
  };

  pdf_resource<PAGE_COLORSPACES>::pdf_resource():
    parent_(nullptr)
  {}

  pdf_resource<PAGE_COLORSPACES>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> parent):
    parent_(parent)
  {}

  pdf_resource<PAGE_COLORSPACES>::~pdf_resource()
  {}

  size_t pdf_resource<PAGE_COLORSPACES>::size()
  {
    return page_colorspaces.size();
  }

  int pdf_resource<PAGE_COLORSPACES>::count(std::string key)
  {
    if(page_colorspaces.count(key)==1)
      {
        return 1;
      }
    if(parent_)
      {
        return parent_->count(key);
      }
    return 0;
  }

  std::unordered_set<std::string> pdf_resource<PAGE_COLORSPACES>::keys()
  {
    std::unordered_set<std::string> keys_;

    if(parent_)
      {
        keys_ = parent_->keys();
      }

    for(auto itr=page_colorspaces.begin(); itr!=page_colorspaces.end(); itr++)
      {
        keys_.insert(itr->first);
      }

    return keys_;
  }

  pdf_resource<PAGE_COLORSPACE>& pdf_resource<PAGE_COLORSPACES>::operator[](std::string cs_name)
  {
    if(page_colorspaces.count(cs_name)==1)
      {
        return page_colorspaces[cs_name];
      }

    if(parent_)
      {
        return (*parent_)[cs_name];
      }

    {
      std::stringstream ss;
      ss << "color space with name '" << cs_name << "' is not known: ";
      for(auto itr=page_colorspaces.begin(); itr!=page_colorspaces.end(); itr++)
        {
          ss << itr->first << ", ";
        }

      LOG_S(ERROR) << ss.str();
      throw std::logic_error(ss.str());
    }

    return (page_colorspaces.begin()->second);
  }

  void pdf_resource<PAGE_COLORSPACES>::set(QPDFObjectHandle& qpdf_colorspaces)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& key : qpdf_colorspaces.getKeys())
      {
        LOG_S(INFO) << "decoding color space: " << key;

        pdf_resource<PAGE_COLORSPACE> page_colorspace;
        page_colorspace.set(key, qpdf_colorspaces.getKey(key));

        page_colorspaces[key] = page_colorspace;
      }
  }

}

#endif
