//-*-C++-*-

#ifndef PDF_PAGE_SHADINGS_RESOURCE_H
#define PDF_PAGE_SHADINGS_RESOURCE_H

namespace pdflib
{

  // The /Shading subdictionary of a resource dictionary. Like the other
  // resource collections it is parent-linked, so a form XObject that defines
  // its own shadings still resolves the ones it merely inherits.
  template<>
  class pdf_resource<PAGE_SHADINGS>
  {
  public:

    pdf_resource();
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_SHADINGS>> parent);
    ~pdf_resource();

    size_t size();

    bool has(const std::string& key) const;

    std::unordered_set<std::string> keys();

    // Returns nullptr when the name is not in this scope nor in any ancestor.
    const pdf_resource<PAGE_SHADING>* get(const std::string& key) const;

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

  bool pdf_resource<PAGE_SHADINGS>::has(const std::string& key) const
  {
    if(page_shadings.count(key) == 1)
      {
        return true;
      }

    if(parent_)
      {
        return parent_->has(key);
      }

    return false;
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

  const pdf_resource<PAGE_SHADING>* pdf_resource<PAGE_SHADINGS>::get(const std::string& key) const
  {
    auto itr = page_shadings.find(key);
    if(itr != page_shadings.end())
      {
        return &(itr->second);
      }

    if(parent_)
      {
        return parent_->get(key);
      }

    return nullptr;
  }

  void pdf_resource<PAGE_SHADINGS>::set(QPDFObjectHandle& qpdf_shadings)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(not qpdf_shadings.isDictionary())
      {
        LOG_S(WARNING) << "/Shading resource is not a dictionary";
        return;
      }

    for(auto& key : qpdf_shadings.getKeys())
      {
        LOG_S(INFO) << "decoding shading: " << key;

        pdf_resource<PAGE_SHADING> page_shading;
        page_shading.set(key, qpdf_shadings.getKey(key));

        page_shadings[key] = page_shading;
      }
  }

}

#endif
