//-*-C++-*-

#ifndef QPDF_ANNOTS_H
#define QPDF_ANNOTS_H

#include <sstream>
#include <iostream>
#include <iomanip>
#include <unordered_set>

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
  
  nlohmann::json extract_document_annotations_in_json(QPDF& pdf_obj,
						      QPDFObjectHandle& root)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    nlohmann::json annots = nlohmann::json::object({});

    annots["form"] = extract_acroform_in_json(pdf_obj, root);
    
    annots["meta_xml"] = extract_metadata_in_json(pdf_obj, root);

    annots["language"] = extract_language_in_json(pdf_obj, root);

    // The table-of-contents is composed by pdf_decoder<DOCUMENT>, not here: it
    // resolves destinations against the page geometry, and pdf_outline therefore
    // depends on page_item<PAGE_DIMENSION>, which is declared after this header.

    LOG_S(INFO) << "annotations: " << annots.dump(2);
    
    return annots;
  }

}

#endif
