//-*-C++-*-

#ifndef PDF_DOCUMENT_INFO_H
#define PDF_DOCUMENT_INFO_H

#include <map>
#include <optional>
#include <string>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/Buffer.hh>

namespace pdflib
{
  // The /Encrypt parameters of a document (ISO 32000-1, 7.6). The permission
  // flags are reported as stored; nothing in docling-parse enforces them.
  class pdf_encryption_info
  {
  public:

    pdf_encryption_info();

    static pdf_encryption_info read(QPDF& qpdf);

    bool is_encrypted() const;

    int get_revision() const;    // /R
    int get_version() const;     // /V
    int get_permissions() const; // /P, the raw signed flag word

    // "none", "rc4", "aes", "aesv3" or "unknown"
    const std::string& get_stream_method() const;
    const std::string& get_string_method() const;
    const std::string& get_file_method() const;

  private:

    static std::string method_name(QPDF::encryption_method_e method);

  private:

    bool encrypted;

    int revision;
    int version;
    int permissions;

    std::string stream_method;
    std::string string_method;
    std::string file_method;
  };

  // Document-scoped facts that belong to no page: header version, the trailer
  // /Info dictionary (ISO 32000-1, 14.3.3), the XMP packet (14.3.2) and the
  // encryption parameters. Typed, self-contained and detached from the QPDF it
  // was read from.
  class pdf_document_info
  {
  public:

    pdf_document_info();

    // Caller serializes access to `qpdf`.
    static pdf_document_info read(QPDF& qpdf);

    const std::string& get_pdf_version() const;
    int get_extension_level() const;

    int get_number_of_pages() const;

    // /Info entries with a string value, keys without the leading slash
    // ("Title", "Author", ...), values in UTF-8.
    const std::map<std::string, std::string>& get_info() const;

    // The raw XMP packet of the catalog's /Metadata stream, if there is one.
    const std::optional<std::string>& get_xmp() const;

    const pdf_encryption_info& get_encryption() const;

    // For a document being edited, the encryption that matters is the one
    // its output will have, which the editor knows and the in-memory QPDF
    // does not.
    void set_encryption(const pdf_encryption_info& encryption_);

  private:

    std::string pdf_version;
    int extension_level;

    int number_of_pages;

    std::map<std::string, std::string> info;
    std::optional<std::string> xmp;

    pdf_encryption_info encryption;
  };

  inline pdf_encryption_info::pdf_encryption_info():
    encrypted(false),
    revision(0),
    version(0),
    permissions(0),
    stream_method("none"),
    string_method("none"),
    file_method("none")
  {}

  inline std::string pdf_encryption_info::method_name(QPDF::encryption_method_e method)
  {
    switch(method)
      {
      case QPDF::e_none:  return "none";
      case QPDF::e_rc4:   return "rc4";
      case QPDF::e_aes:   return "aes";
      case QPDF::e_aesv3: return "aesv3";
      default:            return "unknown";
      }
  }

  inline pdf_encryption_info pdf_encryption_info::read(QPDF& qpdf)
  {
    pdf_encryption_info result;

    int R = 0;
    int P = 0;
    int V = 0;

    QPDF::encryption_method_e stream_method = QPDF::e_none;
    QPDF::encryption_method_e string_method = QPDF::e_none;
    QPDF::encryption_method_e file_method = QPDF::e_none;

    result.encrypted = qpdf.isEncrypted(R, P, V, stream_method, string_method, file_method);

    if(result.encrypted)
      {
        result.revision = R;
        result.version = V;
        result.permissions = P;

        result.stream_method = method_name(stream_method);
        result.string_method = method_name(string_method);
        result.file_method = method_name(file_method);
      }

    return result;
  }

  inline bool pdf_encryption_info::is_encrypted() const { return encrypted; }

  inline int pdf_encryption_info::get_revision() const { return revision; }

  inline int pdf_encryption_info::get_version() const { return version; }

  inline int pdf_encryption_info::get_permissions() const { return permissions; }

  inline const std::string& pdf_encryption_info::get_stream_method() const { return stream_method; }

  inline const std::string& pdf_encryption_info::get_string_method() const { return string_method; }

  inline const std::string& pdf_encryption_info::get_file_method() const { return file_method; }

  inline pdf_document_info::pdf_document_info():
    pdf_version(""),
    extension_level(0),
    number_of_pages(0),
    info({}),
    xmp(std::nullopt),
    encryption()
  {}

  inline pdf_document_info pdf_document_info::read(QPDF& qpdf)
  {
    pdf_document_info result;

    result.pdf_version = qpdf.getPDFVersion();
    result.extension_level = qpdf.getExtensionLevel();

    result.number_of_pages = static_cast<int>(qpdf.getAllPages().size());

    result.encryption = pdf_encryption_info::read(qpdf);

    // /Info is optional and, in damaged files, not always a dictionary.
    QPDFObjectHandle trailer = qpdf.getTrailer();
    if(trailer.isDictionary() and trailer.hasKey("/Info"))
      {
        QPDFObjectHandle info_dict = trailer.getKey("/Info");
        if(info_dict.isDictionary())
          {
            for(const std::string& key : info_dict.getKeys())
              {
                QPDFObjectHandle value = info_dict.getKey(key);
                if(value.isString())
                  {
                    // keys are PDF names: drop the leading slash
                    std::string name = (key.size()>0 and key.front()=='/') ? key.substr(1) : key;
                    result.info[name] = value.getUTF8Value();
                  }
              }
          }
      }

    QPDFObjectHandle root = qpdf.getRoot();
    if(root.isDictionary() and root.hasKey("/Metadata"))
      {
        QPDFObjectHandle metadata = root.getKey("/Metadata");
        if(metadata.isStream())
          {
            std::shared_ptr<Buffer> data = metadata.getStreamData();
            if(data != nullptr)
              {
                result.xmp = std::string(reinterpret_cast<const char*>(data->getBuffer()),
                                         data->getSize());
              }
          }
      }

    return result;
  }

  inline const std::string& pdf_document_info::get_pdf_version() const { return pdf_version; }

  inline int pdf_document_info::get_extension_level() const { return extension_level; }

  inline int pdf_document_info::get_number_of_pages() const { return number_of_pages; }

  inline const std::map<std::string, std::string>& pdf_document_info::get_info() const { return info; }

  inline const std::optional<std::string>& pdf_document_info::get_xmp() const { return xmp; }

  inline const pdf_encryption_info& pdf_document_info::get_encryption() const { return encryption; }

  inline void pdf_document_info::set_encryption(const pdf_encryption_info& encryption_)
  {
    encryption = encryption_;
  }

}

#endif
