//-*-C++-*-

#ifndef QPDF_ANNOTS_H
#define QPDF_ANNOTS_H

#include <sstream>
#include <iostream>
#include <iomanip>

#include <nlohmann/json.hpp>
#include <qpdf/QPDF.hh>

namespace pdflib
{
  // FIXME: add a begin time to cap the max time spent in this routine
  nlohmann::json extract_annots_in_json(QPDFObjectHandle obj,
                                        std::set<std::string> prev_objs={},
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
        result = to_json(obj.getKey("/Annot"), prev_objs, level, max_level);
      }
    else if(level==0 and obj.isDictionary() and
            obj.hasKey("/Annots"))
      {
        result = to_json(obj.getKey("/Annots"), prev_objs, level, max_level);
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
		auto ptr = metadata.getStreamData(qpdf_dl_all);
		
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

  nlohmann::json extract_toc_entry_in_json(QPDF& pdf_obj, QPDFObjectHandle& node, int level)
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

	LOG_S(INFO) << "same: "<< first.unparse() << " == " << node.unparse(); 	
	for(auto key : first.getKeys())
	  {
	    LOG_S(INFO) << "\t" << level << " -> key: " << key;
	  }
	
	bool first_is_not_equal_to_node = (first.unparse()!=node.unparse());
	
        while(first.isDictionary() and first_is_not_equal_to_node)
          {
	    auto child = extract_toc_entry_in_json(pdf_obj, first, level+1);
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

            while(first.isDictionary())
              {
		int level=0;
                toc.push_back(extract_toc_entry_in_json(pdf_obj, first, level));

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
  
  nlohmann::json extract_document_annotations_in_json(QPDF& pdf_obj,
						      QPDFObjectHandle& root)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    nlohmann::json annots = nlohmann::json::object({});

    annots["form"] = extract_acroform_in_json(pdf_obj, root);
    
    annots["meta_xml"] = extract_metadata_in_json(pdf_obj, root);

    annots["language"] = extract_language_in_json(pdf_obj, root);

    annots["table_of_contents"] = extract_toc_in_json(pdf_obj, root);
    
    LOG_S(INFO) << "annotations: " << annots.dump(2);
    
    return annots;
  }

}

#endif
