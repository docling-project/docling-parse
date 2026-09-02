//-*-C++-*-

#ifndef QPDF_ANNOTS_H
#define QPDF_ANNOTS_H

#include <sstream>
#include <iostream>
#include <iomanip>
#include <unordered_set>
#include <map>

#include <nlohmann/json.hpp>
#include <qpdf/QPDF.hh>

#include <parse/qpdf/qpdf_compat.h>

namespace pdflib
{
  // FIXME: add a begin time to cap the max time spent in this routine
  nlohmann::json extract_annots_in_json(QPDFObjectHandle obj,
                                        std::unordered_set<std::string> prev_objs={},
                                        int level=0, int max_level=16)
  {
    LOG_S(INFO) << __FUNCTION__;

    /*
    if(obj.isDictionary())
      {
        for(auto key : obj.getKeys())
          {
            LOG_S(INFO) << "key: " << key;
          }
      }
    */
    
    nlohmann::json result;

    if(level==0 and obj.isDictionary() and
       obj.hasKey("/Annot"))
      {
        QPDFObjectHandle annot = obj.getKey("/Annot");
        if(not annot.isNull())
          {
            result = to_json(annot, prev_objs, level, max_level);
          }
      }
    else if(level==0 and obj.isDictionary() and
            obj.hasKey("/Annots"))
      {
        QPDFObjectHandle annots = obj.getKey("/Annots");
        if(not annots.isNull())
          {
            result = to_json(annots, prev_objs, level, max_level);
          }
      }

    return result;
  }

  /*** Top level Annotations ***/

  nlohmann::json extract_acroform_in_json(QPDF& pdf_obj,
					  QPDFObjectHandle& root)
  {
    nlohmann::json result = nlohmann::json::value_t::null;
    
    if(root.hasKey("/AcroForm"))
      {
	LOG_S(INFO) << "/AcroForm detected!";
	
	try
	  {
	    result = to_json(root.getKey("/AcroForm"), {}, 0, 16);
	  }
	catch(const std::exception& exc)
	  {
	    LOG_S(ERROR) << "encountered exception: " << exc.what(); 
	  }
      }
    else
      {
	LOG_S(INFO) << "no /AcroForm detected ...";
      }

    return result;
  }

  nlohmann::json extract_metadata_in_json(QPDF& pdf_obj,
					  QPDFObjectHandle& root)
  {
    nlohmann::json result = nlohmann::json::value_t::null;

    if(root.hasKey("/Metadata"))
      {
	LOG_S(INFO) << "/Metadata detected!";
	
	try
	  {
	    QPDFObjectHandle metadata = root.getKey("/Metadata");

	    if(metadata.isStream())
	      {
		auto ptr = to_shared_ptr(metadata.getStreamData(qpdf_dl_all));
		
		// Convert raw data to std::string
		std::string content(reinterpret_cast<const char*>(ptr->getBuffer()), ptr->getSize());
		LOG_S(INFO) << "content (1): " << content;
		
		// Remove \r and \n characters
		content = std::regex_replace(content, std::regex("(\\r|\\n)+"), "");
		LOG_S(INFO) << "content (2): " << content;
		
		// Replace multiple spaces with a single space
		content = std::regex_replace(content, std::regex("\\s{2,}"), " ");
		LOG_S(INFO) << "content (3): " << content;
		
		if(utils::string::is_valid_utf8(content))
		  {
		    //LOG_S(INFO) << content;
		    result = content;
		  }
		else
		  {
		    LOG_S(WARNING) << "meta-data is not utf8 compliant, fixing it";
		    
		    std::string content_utf8 = utils::string::fix_into_valid_utf8(content);
		    //LOG_S(INFO) << content;
		    //LOG_S(INFO) << content_utf8;		    

		    result = content_utf8;
		  }
	      }
	    else
	      {
		LOG_S(ERROR) << "metadata is not a stream"; 
	      }
	  }
	catch(const std::exception& exc)
	  {
	    LOG_S(ERROR) << "encountered exception: " << exc.what(); 
	  }
      }
    else
      {
	LOG_S(INFO) << "no /Metadata detected ...";
      }
    
    return result;
  }

  nlohmann::json extract_language_in_json(QPDF& pdf_obj,
					  QPDFObjectHandle& root)
  {
    nlohmann::json result = nlohmann::json::value_t::null;

    if(root.hasKey("/Lang"))
      {
	LOG_S(INFO) << "/Lang detected!";	
	
        std::string lang = root.getKey("/Lang").getUTF8Value();

	std::string lang_utf8 = utils::string::fix_into_valid_utf8(lang);
	result = lang_utf8;
      }
    else
      {
	LOG_S(INFO) << "no /Lang detected ...";	
      }
    
    return result;
  }
  
  /*** Table of Contents ***/

  nlohmann::json extract_toc_entry_in_json(QPDF& pdf_obj, QPDFObjectHandle& node, int level,
                                           std::unordered_set<std::string>& visited)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    nlohmann::json toc_entry = nlohmann::json::object({});

    // securing we dont crash ...
    if(level>=16)
      {
	return toc_entry;
      }

    //for(auto key : node.getKeys())
    //{
    //LOG_S(INFO) << " -> key: " << key;
    //}
    
    // Extract title
    if(node.hasKey("/Title"))
      {
	auto title = node.getKey("/Title");
	
	if(title.isString())
	  {
	    std::string val = title.getUTF8Value();

            if(utf8::is_valid(val.begin(), val.end()))
              {
                toc_entry["title"] = val;
		LOG_S(INFO) << "level: " << level << "\t" << val;
              }
            else
              {
                utf8::replace_invalid(val.begin(), val.end(),
                                      std::back_inserter(val));
                toc_entry["title"] = val;
              }	    
	  }
	else
	  {
	    LOG_S(WARNING) << "title is not a string!";
	    toc_entry["title"] = "<unknown>";
	  }
	
        toc_entry["level"] = level;
      }
    
    // Extract title
    if(node.hasKey("/A"))
      {
        //toc_entry["link"] = to_json(node.getKey("/A"), {}, 0, 8);
      }
    
    // Extract destination
    if(node.hasKey("/Dest"))
      {
	//LOG_S(INFO) << "found a destination!";
	
        // Depending on the type of destination, extract its value
	//auto dest = node.getKey("/Dest");
        //toc_entry["destination"] = to_json(dest, {}, 0, 8);
      }
    else
      {
        //toc_entry["destination"] = "No destination";
      }

    // Extract children
    if (node.hasKey("/First"))
      {
        QPDFObjectHandle first = node.getKey("/First");

        while(first.isDictionary())
          {
            // Check for circular reference
            std::string obj_ref = first.unparse();
            if (visited.count(obj_ref))
              {
                LOG_S(WARNING) << "Circular TOC reference detected, skipping: " << obj_ref;
                break;
              }
            visited.insert(obj_ref);

            auto child = extract_toc_entry_in_json(pdf_obj, first, level+1, visited);
            toc_entry["children"].push_back(child);

            if(first.hasKey("/Next"))
              {
                first = first.getKey("/Next");
              }
            else
              {
                break;
              }
          }
      }

    return toc_entry;
  }
  
  nlohmann::json extract_toc_in_json(QPDF& pdf_obj, QPDFObjectHandle& root)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    nlohmann::json toc = nlohmann::json::value_t::null;

    if(root.hasKey("/Outlines"))
      {
	LOG_S(INFO) << "/Outlines (=table-of-contents) detected!";
	
        QPDFObjectHandle outlines = root.getKey("/Outlines");

        if(outlines.hasKey("/First"))
          {
            QPDFObjectHandle first = outlines.getKey("/First");

            toc = nlohmann::json::array({});

            // Track visited nodes to prevent infinite loops
            std::unordered_set<std::string> visited;

            while(first.isDictionary())
              {
                // Check for circular reference
                std::string obj_ref = first.unparse();
                if (visited.count(obj_ref))
                  {
                    LOG_S(WARNING) << "Circular TOC reference detected at top level, skipping: " << obj_ref;
                    break;
                  }
                visited.insert(obj_ref);

		int level=0;
                toc.push_back(extract_toc_entry_in_json(pdf_obj, first, level, visited));

                if(first.hasKey("/Next"))
                  {
                    first = first.getKey("/Next");
                  }
                else
                  {
                    break;
                  }
              }
          }
      }
    else
      {
        LOG_S(INFO) << "no /Outlines (=table-of-contents) detected ...";
      }

    return toc;
  }
  
  /*** Tagged-PDF logical structure (PDF 32000-2, 14.7) ***/

  // Object identity for structure elements: "obj gen" for indirect objects,
  // an empty string for direct ones (rare, and then not referenceable anyway).
  inline std::string struct_obj_id(QPDFObjectHandle& obj)
  {
    if(obj.isIndirect())
      {
        return std::to_string(obj.getObjectID())+" "+std::to_string(obj.getGeneration());
      }
    return "";
  }

  inline int struct_page_index(QPDFObjectHandle page,
                               const std::map<std::string, int>& page_index)
  {
    if(not page.isDictionary())
      {
        return -1;
      }
    std::string id = struct_obj_id(page);
    auto itr = page_index.find(id);
    return (itr==page_index.end()) ? -1 : itr->second;
  }

  // Attribute objects (/A) are a dictionary or an array of dictionaries, each
  // keyed by its owner (/O): Layout, List, Table, PrintField, Artifact,
  // ARIA-1.1, ... Returned as {owner: {name: value}} with values via to_json.
  inline nlohmann::json struct_attributes_in_json(QPDFObjectHandle attrs)
  {
    nlohmann::json result = nlohmann::json::object({});

    std::vector<QPDFObjectHandle> dicts;
    if(attrs.isDictionary())
      {
        dicts.push_back(attrs);
      }
    else if(attrs.isArray())
      {
        for(int i=0; i<attrs.getArrayNItems(); i++)
          {
            QPDFObjectHandle item = attrs.getArrayItem(i);
            if(item.isDictionary()) { dicts.push_back(item); }
            // integers interleaved in the array are revision numbers: skipped
          }
      }

    for(auto& dict : dicts)
      {
        std::string owner = "";
        if(dict.hasKey("/O") and dict.getKey("/O").isName())
          {
            owner = dict.getKey("/O").getName();
          }

        nlohmann::json values = nlohmann::json::object({});
        for(auto key : dict.getKeys())
          {
            if(key=="/O") { continue; }
            values[key] = to_json(dict.getKey(key), {}, 0, 4);
          }
        result[owner] = values;
      }

    return result;
  }

  inline void struct_string_entry(QPDFObjectHandle& elem, const std::string& key,
                                  const std::string& name, nlohmann::json& out)
  {
    if(elem.hasKey(key) and elem.getKey(key).isString())
      {
        out[name] = utils::string::fix_into_valid_utf8(elem.getKey(key).getUTF8Value());
      }
  }

  nlohmann::json extract_struct_elem_in_json(QPDFObjectHandle elem,
                                             const std::map<std::string, int>& page_index,
                                             std::unordered_set<std::string>& visited,
                                             int& order, int level)
  {
    nlohmann::json result = nlohmann::json::object({});

    if(level>=64 or not elem.isDictionary())
      {
        return result;
      }

    std::string id = struct_obj_id(elem);
    if(not id.empty())
      {
        if(visited.count(id))
          {
            LOG_S(WARNING) << "cyclic structure element reference, skipping: " << id;
            return result;
          }
        visited.insert(id);
      }

    result["id"]    = id;
    result["order"] = order++;

    if(elem.hasKey("/S") and elem.getKey("/S").isName())
      {
        result["type"] = elem.getKey("/S").getName();
      }
    else
      {
        result["type"] = "";
      }

    // PDF 2.0 namespace (14.7.4): /NS is a namespace dictionary with a /NS string
    if(elem.hasKey("/NS") and elem.getKey("/NS").isDictionary())
      {
        QPDFObjectHandle ns = elem.getKey("/NS");
        if(ns.hasKey("/NS") and ns.getKey("/NS").isString())
          {
            result["namespace"] = ns.getKey("/NS").getUTF8Value();
          }
      }

    struct_string_entry(elem, "/T",          "title",       result);
    struct_string_entry(elem, "/Lang",       "lang",        result);
    struct_string_entry(elem, "/Alt",        "alt",         result);
    struct_string_entry(elem, "/ActualText", "actual_text", result);
    struct_string_entry(elem, "/E",          "expansion",   result);

    if(elem.hasKey("/ID") and elem.getKey("/ID").isString())
      {
        result["element_id"] = elem.getKey("/ID").getUTF8Value();
      }

    int elem_page = -1;
    if(elem.hasKey("/Pg"))
      {
        elem_page = struct_page_index(elem.getKey("/Pg"), page_index);
        result["page"] = elem_page;
      }

    if(elem.hasKey("/A"))
      {
        result["attributes"] = struct_attributes_in_json(elem.getKey("/A"));
      }

    if(elem.hasKey("/Ref") and elem.getKey("/Ref").isArray())
      {
        nlohmann::json refs = nlohmann::json::array({});
        QPDFObjectHandle ref = elem.getKey("/Ref");
        for(int i=0; i<ref.getArrayNItems(); i++)
          {
            QPDFObjectHandle item = ref.getArrayItem(i);
            refs.push_back(struct_obj_id(item));
          }
        result["ref"] = refs;
      }

    // Kids (14.7.5.2): a single kid or an array of kids, each being an
    // integer MCID on the element's page, a marked-content reference dict
    // (/Type /MCR), an object reference dict (/Type /OBJR), or a child element.
    nlohmann::json kids = nlohmann::json::array({});

    std::vector<QPDFObjectHandle> kid_items;
    if(elem.hasKey("/K"))
      {
        QPDFObjectHandle k = elem.getKey("/K");
        if(k.isArray())
          {
            for(int i=0; i<k.getArrayNItems(); i++) { kid_items.push_back(k.getArrayItem(i)); }
          }
        else if(not k.isNull())
          {
            kid_items.push_back(k);
          }
      }

    for(auto& kid : kid_items)
      {
        if(kid.isInteger())
          {
            kids.push_back({{"kind", "mcid"}, {"page", elem_page},
                            {"mcid", static_cast<int>(kid.getIntValue())}});
          }
        else if(kid.isDictionary())
          {
            std::string type = "";
            if(kid.hasKey("/Type") and kid.getKey("/Type").isName())
              {
                type = kid.getKey("/Type").getName();
              }

            if(type=="/MCR")
              {
                int page = elem_page;
                if(kid.hasKey("/Pg")) { page = struct_page_index(kid.getKey("/Pg"), page_index); }
                int mcid = -1;
                if(kid.hasKey("/MCID") and kid.getKey("/MCID").isInteger())
                  {
                    mcid = static_cast<int>(kid.getKey("/MCID").getIntValue());
                  }
                kids.push_back({{"kind", "mcid"}, {"page", page}, {"mcid", mcid}});
              }
            else if(type=="/OBJR")
              {
                int page = elem_page;
                if(kid.hasKey("/Pg")) { page = struct_page_index(kid.getKey("/Pg"), page_index); }
                std::string obj = "";
                std::string subtype = "";
                if(kid.hasKey("/Obj"))
                  {
                    QPDFObjectHandle target = kid.getKey("/Obj");
                    obj = struct_obj_id(target);
                    if(target.isDictionary() and target.hasKey("/Subtype") and
                       target.getKey("/Subtype").isName())
                      {
                        subtype = target.getKey("/Subtype").getName();
                      }
                  }
                kids.push_back({{"kind", "objref"}, {"page", page}, {"obj", obj}, {"subtype", subtype}});
              }
            else
              {
                nlohmann::json child = extract_struct_elem_in_json(kid, page_index, visited, order, level+1);
                if(not child.empty())
                  {
                    child["kind"] = "element";
                    kids.push_back(child);
                  }
              }
          }
      }

    result["kids"] = kids;
    return result;
  }

  nlohmann::json extract_structure_in_json(QPDF& pdf_obj, QPDFObjectHandle& root,
                                           const std::vector<QPDFObjectHandle>& pages)
  {
    LOG_S(INFO) << __FUNCTION__;

    nlohmann::json structure = nlohmann::json::value_t::null;

    if(not root.hasKey("/StructTreeRoot") or not root.getKey("/StructTreeRoot").isDictionary())
      {
        LOG_S(INFO) << "no /StructTreeRoot detected ...";
        return structure;
      }

    QPDFObjectHandle tree = root.getKey("/StructTreeRoot");
    structure = nlohmann::json::object({});

    // /MarkInfo /Marked declares the file as tagged (14.7.1)
    bool marked = false;
    if(root.hasKey("/MarkInfo") and root.getKey("/MarkInfo").isDictionary())
      {
        QPDFObjectHandle mark_info = root.getKey("/MarkInfo");
        if(mark_info.hasKey("/Marked") and mark_info.getKey("/Marked").isBool())
          {
            marked = mark_info.getKey("/Marked").getBoolValue();
          }
      }
    structure["marked"] = marked;

    std::map<std::string, int> page_index;
    for(std::size_t i=0; i<pages.size(); i++)
      {
        QPDFObjectHandle page = pages.at(i);
        page_index[struct_obj_id(page)] = static_cast<int>(i);
      }

    nlohmann::json role_map = nlohmann::json::object({});
    if(tree.hasKey("/RoleMap") and tree.getKey("/RoleMap").isDictionary())
      {
        QPDFObjectHandle rm = tree.getKey("/RoleMap");
        for(auto key : rm.getKeys())
          {
            QPDFObjectHandle val = rm.getKey(key);
            if(val.isName()) { role_map[key] = val.getName(); }
          }
      }
    structure["role_map"] = role_map;

    if(tree.hasKey("/Namespaces") and tree.getKey("/Namespaces").isArray())
      {
        nlohmann::json namespaces = nlohmann::json::array({});
        QPDFObjectHandle nss = tree.getKey("/Namespaces");
        for(int i=0; i<nss.getArrayNItems(); i++)
          {
            QPDFObjectHandle ns = nss.getArrayItem(i);
            if(ns.isDictionary() and ns.hasKey("/NS") and ns.getKey("/NS").isString())
              {
                namespaces.push_back(ns.getKey("/NS").getUTF8Value());
              }
          }
        structure["namespaces"] = namespaces;
      }

    std::unordered_set<std::string> visited;
    int order = 0;
    nlohmann::json elements = nlohmann::json::array({});

    std::vector<QPDFObjectHandle> roots;
    if(tree.hasKey("/K"))
      {
        QPDFObjectHandle k = tree.getKey("/K");
        if(k.isArray())
          {
            for(int i=0; i<k.getArrayNItems(); i++) { roots.push_back(k.getArrayItem(i)); }
          }
        else if(k.isDictionary())
          {
            roots.push_back(k);
          }
      }

    for(auto& elem : roots)
      {
        nlohmann::json e = extract_struct_elem_in_json(elem, page_index, visited, order, 0);
        if(not e.empty()) { elements.push_back(e); }
      }
    structure["elements"] = elements;

    LOG_S(INFO) << "structure tree: " << order << " element(s), marked=" << marked;
    return structure;
  }

  nlohmann::json extract_document_annotations_in_json(QPDF& pdf_obj,
						      QPDFObjectHandle& root,
						      const std::vector<QPDFObjectHandle>& pages)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    nlohmann::json annots = nlohmann::json::object({});

    annots["form"] = extract_acroform_in_json(pdf_obj, root);
    
    annots["meta_xml"] = extract_metadata_in_json(pdf_obj, root);

    annots["language"] = extract_language_in_json(pdf_obj, root);

    annots["table_of_contents"] = extract_toc_in_json(pdf_obj, root);

    annots["structure"] = extract_structure_in_json(pdf_obj, root, pages);

    LOG_S(INFO) << "annotations: " << annots.dump(2);
    
    return annots;
  }

}

#endif
