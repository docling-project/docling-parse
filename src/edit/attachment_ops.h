//-*-C++-*-

#ifndef PDF_ATTACHMENT_OPS_H
#define PDF_ATTACHMENT_OPS_H

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QUtil.hh>

#include <edit/edit_error.h>

namespace pdflib
{
  // Files embedded in the document (ISO 32000-1, 7.11.4) and document-level
  // JavaScript (12.6.4.16). Scripts are data here: they are stored, listed
  // and removed, never run.
  class pdf_attachment_ops
  {
  public:

    // [{"name", "filename", "description", "size", "mime_type",
    //   "creation_date", "modification_date"}, ...], ordered by name
    static nlohmann::json read(QPDF& qpdf);

    // The bytes of the attachment registered under `name`.
    static std::string data(QPDF& qpdf, const std::string& name);

    // Add an attachment, or replace the one with the same name.
    static void add(QPDF& qpdf,
                    const std::string& name,
                    const std::string& bytes,
                    const std::string& description,
                    const std::string& mime_type);

    // False when there is no attachment of that name.
    static bool remove(QPDF& qpdf, const std::string& name);

    // {name: script} of the document-level scripts.
    static nlohmann::json javascript(QPDF& qpdf);

    // Register a script that a viewer runs when it opens the document.
    static void add_javascript(QPDF& qpdf, const std::optional<std::string>& name,
                               const std::string& script);

    // Remove the document-level scripts, a scripted open action and the
    // document's additional actions. Returns the number of entries removed.
    static int remove_javascript(QPDF& qpdf);

  private:

    static std::string script_of(QPDFObjectHandle action);
  };

  inline nlohmann::json pdf_attachment_ops::read(QPDF& qpdf)
  {
    nlohmann::json result = nlohmann::json::array();

    QPDFEmbeddedFileDocumentHelper helper(qpdf);
    if(not helper.hasEmbeddedFiles())
      {
        return result;
      }

    for(auto& item : helper.getEmbeddedFiles())
      {
        nlohmann::json entry = nlohmann::json::object();

        entry["name"] = item.first;
        entry["filename"] = item.second->getFilename();
        entry["description"] = item.second->getDescription();

        entry["size"] = 0;
        entry["mime_type"] = "";
        entry["creation_date"] = "";
        entry["modification_date"] = "";

        QPDFObjectHandle stream = item.second->getEmbeddedFileStream();
        if(stream.isStream())
          {
            QPDFEFStreamObjectHelper embedded(stream);

            entry["size"] = embedded.getSize();
            entry["mime_type"] = embedded.getSubtype();
            entry["creation_date"] = embedded.getCreationDate();
            entry["modification_date"] = embedded.getModDate();
          }

        result.push_back(entry);
      }

    return result;
  }

  inline std::string pdf_attachment_ops::data(QPDF& qpdf, const std::string& name)
  {
    QPDFEmbeddedFileDocumentHelper helper(qpdf);

    std::shared_ptr<QPDFFileSpecObjectHelper> file = helper.getEmbeddedFile(name);
    if(file == nullptr)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "the document has no attachment named '" + name + "'");
      }

    QPDFObjectHandle stream = file->getEmbeddedFileStream();
    if(not stream.isStream())
      {
        throw pdf_edit_error(load_error::DAMAGED,
                             "attachment '" + name + "' has no embedded file stream");
      }

    std::shared_ptr<Buffer> buffer = stream.getStreamData(qpdf_dl_all);
    if(buffer == nullptr)
      {
        return std::string();
      }

    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
  }

  inline void pdf_attachment_ops::add(QPDF& qpdf,
                                      const std::string& name,
                                      const std::string& bytes,
                                      const std::string& description,
                                      const std::string& mime_type)
  {
    if(name.empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "an attachment needs a name");
      }

    QPDFEFStreamObjectHelper embedded = QPDFEFStreamObjectHelper::createEFStream(qpdf, bytes);

    std::string now = QUtil::qpdf_time_to_pdf_time(QUtil::get_current_qpdf_time());
    embedded.setCreationDate(now);
    embedded.setModDate(now);
    if(not mime_type.empty())
      {
        embedded.setSubtype(mime_type);
      }

    QPDFFileSpecObjectHelper file = QPDFFileSpecObjectHelper::createFileSpec(qpdf, name, embedded);
    if(not description.empty())
      {
        file.setDescription(description);
      }

    QPDFEmbeddedFileDocumentHelper helper(qpdf);
    helper.replaceEmbeddedFile(name, file);
  }

  inline bool pdf_attachment_ops::remove(QPDF& qpdf, const std::string& name)
  {
    QPDFEmbeddedFileDocumentHelper helper(qpdf);
    return helper.removeEmbeddedFile(name);
  }

  inline std::string pdf_attachment_ops::script_of(QPDFObjectHandle action)
  {
    if(not action.isDictionary())
      {
        return "";
      }

    // /JS is a text string or a stream holding one.
    QPDFObjectHandle script = action.getKey("/JS");
    if(script.isString())
      {
        return script.getUTF8Value();
      }
    if(script.isStream())
      {
        std::shared_ptr<Buffer> buffer = script.getStreamData(qpdf_dl_all);
        if(buffer != nullptr)
          {
            return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
          }
      }

    return "";
  }

  inline nlohmann::json pdf_attachment_ops::javascript(QPDF& qpdf)
  {
    nlohmann::json result = nlohmann::json::object();

    QPDFObjectHandle names = qpdf.getRoot().getKey("/Names");
    if(not (names.isDictionary() and names.getKey("/JavaScript").isDictionary()))
      {
        return result;
      }

    QPDFNameTreeObjectHelper tree(names.getKey("/JavaScript"), qpdf);
    for(auto itr = tree.begin(); itr != tree.end(); ++itr)
      {
        result[itr->first] = script_of(itr->second);
      }

    return result;
  }

  inline void pdf_attachment_ops::add_javascript(QPDF& qpdf,
                                                 const std::optional<std::string>& name,
                                                 const std::string& script)
  {
    if(name and name->empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a script needs a name");
      }

    QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
    action.replaceKey("/Type", QPDFObjectHandle::newName("/Action"));
    action.replaceKey("/S", QPDFObjectHandle::newName("/JavaScript"));
    action.replaceKey("/JS", QPDFObjectHandle::newUnicodeString(script));
    action = qpdf.makeIndirectObject(action);

    QPDFObjectHandle root = qpdf.getRoot();

    QPDFObjectHandle names = root.getKey("/Names");
    if(not names.isDictionary())
      {
        names = QPDFObjectHandle::newDictionary();
        root.replaceKey("/Names", names);
      }

    QPDFObjectHandle existing = names.getKey("/JavaScript");
    auto tree = existing.isDictionary() ? QPDFNameTreeObjectHelper(existing, qpdf)
                                        : QPDFNameTreeObjectHelper::newEmpty(qpdf);
    std::string key = name.value_or("script-1");
    for(std::size_t number = 2; not name and tree.hasName(key); ++number)
      {
        key = "script-" + std::to_string(number);
      }
    tree.insert(key, action);
    if(not existing.isDictionary())
      {
        names.replaceKey("/JavaScript", tree.getObjectHandle());
      }
  }

  inline int pdf_attachment_ops::remove_javascript(QPDF& qpdf)
  {
    int removed = 0;

    QPDFObjectHandle root = qpdf.getRoot();

    QPDFObjectHandle names = root.getKey("/Names");
    if(names.isDictionary() and names.hasKey("/JavaScript"))
      {
        QPDFObjectHandle scripts = names.getKey("/JavaScript");
        if(scripts.isDictionary())
          {
            QPDFNameTreeObjectHelper tree(scripts, qpdf);
            for(auto itr = tree.begin(); itr != tree.end(); ++itr)
              {
                removed += 1;
              }
          }
        names.removeKey("/JavaScript");
      }

    QPDFObjectHandle open_action = root.getKey("/OpenAction");
    if(open_action.isDictionary() and open_action.getKey("/S").isNameAndEquals("/JavaScript"))
      {
        root.removeKey("/OpenAction");
        removed += 1;
      }

    // Document-level additional actions (will close, will save, ...) exist
    // to run scripts.
    if(root.hasKey("/AA"))
      {
        root.removeKey("/AA");
        removed += 1;
      }

    return removed;
  }

}

#endif
