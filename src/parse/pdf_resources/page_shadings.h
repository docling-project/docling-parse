//-*-C++-*-

#ifndef PDF_PAGE_SHADINGS_RESOURCE_H
#define PDF_PAGE_SHADINGS_RESOURCE_H

namespace pdflib
{

  // The /Shading sub-dictionary of a resource dictionary. Mirrors
  // pdf_resource<PAGE_COLORSPACES>: a name -> resource map that falls back to
  // the enclosing dictionary, so a form XObject sees the page's shadings.
  template<>
  class pdf_resource<PAGE_SHADINGS>
  {
  public:

    pdf_resource();
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_SHADINGS>> parent);
    ~pdf_resource();

    size_t size();

    int count(std::string key);

    std::unordered_set<std::string> keys();

    pdf_resource<PAGE_SHADING>& operator[](std::string name);

    void set(QPDFObjectHandle& qpdf_shadings);

  private:

    std::shared_ptr<pdf_resource<PAGE_SHADINGS>> parent_;
    std::unordered_map<std::string, pdf_resource<PAGE_SHADING> > page_shadings;
  };

  pdf_resource<PAGE_SHADINGS>::pdf_resource():
    parent_(nullptr)
  {}

  pdf_resource<PAGE_SHADINGS>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_SHADINGS>> parent):
    parent_(parent)
  {}

  pdf_resource<PAGE_SHADINGS>::~pdf_resource()
  {}

  size_t pdf_resource<PAGE_SHADINGS>::size()
  {
    return page_shadings.size();
  }

  int pdf_resource<PAGE_SHADINGS>::count(std::string key)
  {
    if(page_shadings.count(key)==1)
      {
        return 1;
      }
    if(parent_)
      {
        return parent_->count(key);
      }
    return 0;
  }

  std::unordered_set<std::string> pdf_resource<PAGE_SHADINGS>::keys()
  {
    std::unordered_set<std::string> keys_;

    if(parent_)
      {
        keys_ = parent_->keys();
      }

    for(auto itr=page_shadings.begin(); itr!=page_shadings.end(); itr++)
      {
        keys_.insert(itr->first);
      }

    return keys_;
  }

  pdf_resource<PAGE_SHADING>& pdf_resource<PAGE_SHADINGS>::operator[](std::string name)
  {
    if(page_shadings.count(name)==1)
      {
        return page_shadings[name];
      }

    if(parent_)
      {
        return (*parent_)[name];
      }

    // Unknown name: hand back an unsupported shading rather than throwing, so
    // a malformed `sh` simply paints nothing.
    return page_shadings[name];
  }

  void pdf_resource<PAGE_SHADINGS>::set(QPDFObjectHandle& qpdf_shadings)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(not qpdf_shadings.isDictionary())
      {
        return;
      }

    for(auto& key : qpdf_shadings.getKeys())
      {
        pdf_resource<PAGE_SHADING> shading;
        shading.set(key, qpdf_shadings.getKey(key));

        page_shadings.erase(key);
        page_shadings.emplace(key, std::move(shading));
      }
  }

}

#endif
