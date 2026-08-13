//-*-C++-*-

#ifndef PDF_PAGE_PATTERNS_RESOURCE_H
#define PDF_PAGE_PATTERNS_RESOURCE_H

namespace pdflib
{

  // The /Pattern sub-dictionary of a resource dictionary. Mirrors
  // pdf_resource<PAGE_COLORSPACES>: a name -> resource map that falls back to
  // the enclosing dictionary, so a form XObject sees the page's patterns.
  template<>
  class pdf_resource<PAGE_PATTERNS>
  {
  public:

    pdf_resource();
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_PATTERNS>> parent);
    ~pdf_resource();

    size_t size();

    int count(std::string key);

    std::unordered_set<std::string> keys();

    pdf_resource<PAGE_PATTERN>& operator[](std::string name);

    void set(QPDFObjectHandle& qpdf_patterns);

  private:

    std::shared_ptr<pdf_resource<PAGE_PATTERNS>> parent_;
    std::unordered_map<std::string, pdf_resource<PAGE_PATTERN> > page_patterns;
  };

  pdf_resource<PAGE_PATTERNS>::pdf_resource():
    parent_(nullptr)
  {}

  pdf_resource<PAGE_PATTERNS>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_PATTERNS>> parent):
    parent_(parent)
  {}

  pdf_resource<PAGE_PATTERNS>::~pdf_resource()
  {}

  size_t pdf_resource<PAGE_PATTERNS>::size()
  {
    return page_patterns.size();
  }

  int pdf_resource<PAGE_PATTERNS>::count(std::string key)
  {
    if(page_patterns.count(key)==1)
      {
        return 1;
      }
    if(parent_)
      {
        return parent_->count(key);
      }
    return 0;
  }

  std::unordered_set<std::string> pdf_resource<PAGE_PATTERNS>::keys()
  {
    std::unordered_set<std::string> keys_;

    if(parent_)
      {
        keys_ = parent_->keys();
      }

    for(auto itr=page_patterns.begin(); itr!=page_patterns.end(); itr++)
      {
        keys_.insert(itr->first);
      }

    return keys_;
  }

  pdf_resource<PAGE_PATTERN>& pdf_resource<PAGE_PATTERNS>::operator[](std::string name)
  {
    if(page_patterns.count(name)==1)
      {
        return page_patterns[name];
      }

    if(parent_)
      {
        return (*parent_)[name];
      }

    // Unknown name: hand back an invalid pattern rather than throwing, so a
    // fill naming a missing pattern simply paints nothing.
    return page_patterns[name];
  }

  void pdf_resource<PAGE_PATTERNS>::set(QPDFObjectHandle& qpdf_patterns)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(not qpdf_patterns.isDictionary())
      {
        return;
      }

    for(auto& key : qpdf_patterns.getKeys())
      {
        pdf_resource<PAGE_PATTERN> pattern;
        pattern.set(key, qpdf_patterns.getKey(key));

        page_patterns.erase(key);
        page_patterns.emplace(key, std::move(pattern));
      }
  }

}

#endif
