#ifndef QPDF_ATTACHMENTS_H
#define QPDF_ATTACHMENTS_H

#include <array>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFObjGen.hh>

#include <parse/qpdf/logger.h>
#include <parse/qpdf/qpdf_compat.h>
#include <parse/utils/string.h>

namespace pdflib
{

struct AttachmentAnnotation
{
  int page_no = -1; // 0-based
  std::array<double, 4> bbox = {0, 0, 0, 0};
};

struct AttachmentRecord
{
  std::string name;
  std::string mime_type; // empty if missing
  long long size = 0;
  // QPDF object identity of the EF stream. A non-indirect (0,0) value is the
  // sentinel for a direct (non-indirect) EF stream: the stream dict is
  // embedded inline in the FileSpec and has no indirect object ID, so it
  // cannot be re-fetched via getObjectByID / getObjectByObjGen.
  // Direct streams are intentionally distinct per dedup logic and are not
  // cached; get_attachment_data() detects this sentinel and throws.
  // An indirect stream genuinely at object 0 is not possible in a valid PDF
  // (object 0 is the free-list head), so 0 is unambiguous here.
  QPDFObjGen obj_gen;
  std::vector<AttachmentAnnotation> annotations;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline QPDFObjectHandle getStreamDict(QPDFObjectHandle oh)
{
  return oh.isStream() ? oh.getDict() : oh;
}

inline std::string extract_attachment_name(QPDFObjectHandle fs)
{
  try
    {
      if(fs.hasKey("/UF") && fs.getKey("/UF").isString())
        {
          std::string v = fs.getKey("/UF").getUTF8Value();
          if(!v.empty())
            return utils::string::fix_into_valid_utf8(v);
        }
      if(fs.hasKey("/F") && fs.getKey("/F").isString())
        {
          std::string v = fs.getKey("/F").getUTF8Value();
          if(!v.empty())
            return utils::string::fix_into_valid_utf8(v);
        }
    }
  catch(const std::exception& exc)
    {
      LOG_S(WARNING) << "failed to extract attachment name: " << exc.what();
    }
  return "attachment";
}

inline std::string extract_attachment_mime(QPDFObjectHandle stream_obj)
{
  try
    {
      QPDFObjectHandle dict = getStreamDict(stream_obj);
      if(dict.hasKey("/Subtype") && dict.getKey("/Subtype").isName())
        {
          std::string m = dict.getKey("/Subtype").getName();
          if(!m.empty() && m[0]=='/')
            m = m.substr(1);
          return utils::string::fix_into_valid_utf8(m);
        }
    }
  catch(const std::exception& exc)
    {
      LOG_S(WARNING) << "failed to extract mime: " << exc.what();
    }
  return "";
}

inline long long extract_attachment_size(QPDFObjectHandle stream_obj)
{
  try
    {
      QPDFObjectHandle dict = getStreamDict(stream_obj);
      if(dict.hasKey("/Params") && dict.getKey("/Params").isDictionary())
        {
          QPDFObjectHandle params = dict.getKey("/Params");
          if(params.hasKey("/Size") && params.getKey("/Size").isInteger())
            return params.getKey("/Size").getIntValue();
        }
      if(dict.hasKey("/DL") && dict.getKey("/DL").isInteger())
        return dict.getKey("/DL").getIntValue();
      if(dict.hasKey("/Length") && dict.getKey("/Length").isInteger())
        return dict.getKey("/Length").getIntValue();
    }
  catch(const std::exception& exc)
    {
      LOG_S(WARNING) << "failed to extract size: " << exc.what();
    }
  return 0;
}

inline bool extract_rect(QPDFObjectHandle rect, std::array<double,4>& out)
{
  if(!rect.isArray() || rect.getArrayNItems()!=4)
    return false;
  try
    {
      for(int i=0;i<4;++i)
        out[i] = rect.getArrayItem(i).getNumericValue();
      return true;
    }
  catch(const std::exception& exc)
    {
      LOG_S(WARNING) << "malformed Rect: " << exc.what();
      return false;
    }
}

// One annotation entry for an attachment; bbox zeroed when /Rect is malformed.
inline AttachmentAnnotation make_attachment_annotation(int page_no, QPDFObjectHandle rect)
{
  AttachmentAnnotation ann;
  ann.page_no = page_no;
  if(!extract_rect(rect, ann.bbox))
    ann.bbox = {0,0,0,0};
  return ann;
}

// ---------------------------------------------------------------------------
// Core extraction — metadata only, never calls getStreamData()
// ---------------------------------------------------------------------------

inline std::vector<AttachmentRecord> extract_attachment_records(QPDF& pdf, QPDFObjectHandle& root)
{
  std::vector<AttachmentRecord> records;
  std::map<QPDFObjGen, size_t> obj_to_index;

  auto process_filespec = [&](QPDFObjectHandle fs, int page_no, QPDFObjectHandle rect) {
    if(!fs.isDictionary())
      {
        LOG_S(WARNING) << "FileSpec is not a dict, skipping";
        return;
      }

    std::string name = extract_attachment_name(fs);

    if(!fs.hasKey("/EF") || !fs.getKey("/EF").isDictionary())
      {
        LOG_S(WARNING) << "FileSpec missing /EF for '" << name << "', skipping";
        return;
      }

    QPDFObjectHandle ef_dict = fs.getKey("/EF");
    QPDFObjectHandle stream_obj;

    if(ef_dict.hasKey("/UF") && ef_dict.getKey("/UF").isStream())
      stream_obj = ef_dict.getKey("/UF");
    else if(ef_dict.hasKey("/F") && ef_dict.getKey("/F").isStream())
      stream_obj = ef_dict.getKey("/F");
    else
      {
        LOG_S(WARNING) << "EF dict has no stream for '" << name << "', skipping";
        return;
      }

    if(!stream_obj.isStream())
      {
        LOG_S(WARNING) << "EF entry is not a stream for '" << name << "'";
        return;
      }

    QPDFObjGen og = stream_obj.getObjGen();
    // Direct (non-indirect) EF streams are embedded inline in the FileSpec
    // dict. Each occurrence is a distinct object by definition, so no dedup
    // is attempted for them. Only indirect streams have a stable object
    // identity that can be shared across FileSpecs/annotations.
    bool is_dedupable = og.isIndirect();

    std::string mime = extract_attachment_mime(stream_obj);
    long long size = extract_attachment_size(stream_obj);

    if(is_dedupable && obj_to_index.count(og))
      {
        size_t idx = obj_to_index[og];
        if(page_no>=0)
          records[idx].annotations.push_back(make_attachment_annotation(page_no, rect));
        return;
      }

    AttachmentRecord rec;
    rec.name = name;
    rec.mime_type = mime;
    rec.size = size;
    rec.obj_gen = og;

    if(page_no>=0)
      rec.annotations.push_back(make_attachment_annotation(page_no, rect));

    records.push_back(std::move(rec));
    if(is_dedupable)
      obj_to_index[og] = records.size()-1;
  };

  try
    {
      if(root.hasKey("/Names") && root.getKey("/Names").isDictionary())
        {
          QPDFObjectHandle names = root.getKey("/Names");
          if(names.hasKey("/EmbeddedFiles") && names.getKey("/EmbeddedFiles").isDictionary())
            {
              QPDFObjectHandle ef_tree = names.getKey("/EmbeddedFiles");
              std::function<void(QPDFObjectHandle)> walk = [&](QPDFObjectHandle node){
                if(!node.isDictionary()) return;
                if(node.hasKey("/Names") && node.getKey("/Names").isArray())
                  {
                    QPDFObjectHandle arr = node.getKey("/Names");
                    int n = arr.getArrayNItems();
                    for(int i=1;i<n;i+=2)
                      {
                        try { process_filespec(arr.getArrayItem(i), -1, QPDFObjectHandle::newNull()); }
                        catch(const std::exception& exc) { LOG_S(WARNING) << "walk Names failed: " << exc.what(); }
                      }
                  }
                if(node.hasKey("/Nums") && node.getKey("/Nums").isArray())
                  {
                    QPDFObjectHandle arr = node.getKey("/Nums");
                    int n = arr.getArrayNItems();
                    for(int i=1;i<n;i+=2)
                      {
                        try { process_filespec(arr.getArrayItem(i), -1, QPDFObjectHandle::newNull()); }
                        catch(const std::exception& exc) { LOG_S(WARNING) << "walk Nums failed: " << exc.what(); }
                      }
                  }
                if(node.hasKey("/Kids") && node.getKey("/Kids").isArray())
                  {
                    QPDFObjectHandle kids = node.getKey("/Kids");
                    for(int i=0;i<kids.getArrayNItems();++i)
                      {
                        try { walk(kids.getArrayItem(i)); }
                        catch(const std::exception& exc) { LOG_S(WARNING) << "walk Kids failed: " << exc.what(); }
                      }
                  }
              };
              walk(ef_tree);
            }
        }
    }
  catch(const std::exception& exc)
    {
      LOG_S(WARNING) << "failed walking EmbeddedFiles tree: " << exc.what();
    }

  try
    {
      std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
      for(size_t p=0; p<pages.size(); ++p)
        {
          QPDFObjectHandle page = pages[p];
          int page_no = static_cast<int>(p);
          if(!page.hasKey("/Annots"))
            continue;
          QPDFObjectHandle annots = page.getKey("/Annots");
          if(!annots.isArray())
            continue;
          for(int i=0;i<annots.getArrayNItems();++i)
            {
              try
                {
                  QPDFObjectHandle annot = annots.getArrayItem(i);
                  if(!annot.isDictionary()) continue;
                  if(!annot.hasKey("/Subtype") || !annot.getKey("/Subtype").isName()) continue;
                  if(annot.getKey("/Subtype").getName() != "/FileAttachment") continue;
                  if(!annot.hasKey("/FS")) continue;
                  QPDFObjectHandle fs = annot.getKey("/FS");
                  QPDFObjectHandle rect = annot.hasKey("/Rect") ? annot.getKey("/Rect") : QPDFObjectHandle::newNull();
                  process_filespec(fs, page_no, rect);
                }
              catch(const std::exception& exc)
                {
                  LOG_S(WARNING) << "failed processing annot on page " << p << ": " << exc.what();
                }
            }
        }
    }
  catch(const std::exception& exc)
    {
      LOG_S(WARNING) << "failed walking page annots: " << exc.what();
    }

  return records;
}

inline nlohmann::json attachments_to_json(const std::vector<AttachmentRecord>& records)
{
  nlohmann::json arr = nlohmann::json::array();
  for(const auto& r : records)
    {
      nlohmann::json item;
      item["name"] = r.name;
      item["mime_type"] = r.mime_type.empty() ? nlohmann::json(nullptr) : nlohmann::json(r.mime_type);
      item["size"] = r.size;
      nlohmann::json anns = nlohmann::json::array();
      for(const auto& a : r.annotations)
        {
          nlohmann::json ja;
          ja["page_no"] = a.page_no;
          ja["bbox"] = nlohmann::json::array({a.bbox[0], a.bbox[1], a.bbox[2], a.bbox[3]});
          anns.push_back(ja);
        }
      item["annotations"] = anns;
      arr.push_back(std::move(item));
    }
  return arr;
}

} // namespace pdflib

#endif
