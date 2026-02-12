//-*-C++-*-

#ifndef PAGE_ITEM_DIMENSION_H
#define PAGE_ITEM_DIMENSION_H

namespace pdflib
{

  template<>
  class page_item<PAGE_DIMENSION>
  {
  public:

    page_item();
    ~page_item();
    
    void set_page_boundaries(std::string page_boundary);
    
    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    int get_angle() { return angle; }

    std::array<double, 4> get_crop_bbox() { return crop_bbox; }
    std::array<double, 4> get_media_bbox() { return media_bbox; }

    void execute(QPDFObjectHandle qpdf_page);

    std::pair<double, double> rotate(int angle);
    
  private:

    std::array<double, 4> normalize_page_boundaries(std::array<double, 4> bbox, std::string name);
    std::array<double, 4> qpdf_bbox_to_array(QPDFObjectHandle qpdf_arr, std::string name);

  private:

    bool                  initialised;

    std::string page_boundary;
    
    int                   angle;
    std::array<double, 4> bbox;

    std::array<double, 4> media_bbox;
    std::array<double, 4> crop_bbox;
    std::array<double, 4> bleed_bbox;
    std::array<double, 4> trim_bbox;
    std::array<double, 4> art_bbox;
  };

  page_item<PAGE_DIMENSION>::page_item():
    initialised(false),
    page_boundary(""),
    
    angle(0),
    bbox({0,0,0,0}),

    media_bbox({0,0,0,0}),
    crop_bbox({0,0,0,0}),
    bleed_bbox({0,0,0,0}),
    trim_bbox({0,0,0,0}),
    art_bbox({0,0,0,0})
  {}

  page_item<PAGE_DIMENSION>::~page_item()
  {}

  void page_item<PAGE_DIMENSION>::set_page_boundaries(std::string page_boundary_)
  {
    page_boundary = page_boundary_;
    
    if(page_boundary=="media_box")
      {
	bbox = {0.0, 0.0, media_bbox[2]-media_bbox[0], media_bbox[3]-media_bbox[1]};
      }
    else if(page_boundary=="crop_box")
      {
	bbox = {0.0, 0.0, crop_bbox[2]-crop_bbox[0], crop_bbox[3]-crop_bbox[1]};
      }
    else
      {
	LOG_S(ERROR) << "unsupported page-boundary: " << page_boundary;

	page_boundary = "crop_box";	
	bbox = {0.0, 0.0, crop_bbox[2]-crop_bbox[0], crop_bbox[3]-crop_bbox[1]};
      }
  }
  
  nlohmann::json page_item<PAGE_DIMENSION>::get()
  {
    nlohmann::json result;
    {
      result["page_boundary"] = page_boundary;
      
      result["bbox"] = bbox;
      result["angle"] = angle;
      
      result["width"]  = (bbox[2]-bbox[0]);
      result["height"] = (bbox[3]-bbox[1]);

      result["rectangles"]["media-bbox"] = media_bbox;
      result["rectangles"]["crop-bbox"] = crop_bbox;
      result["rectangles"]["bleed-bbox"] = bleed_bbox;
      result["rectangles"]["trim-bbox"] = trim_bbox;
      result["rectangles"]["art-bbox"] = art_bbox;
    }
    
    return result;
  }

  std::pair<double, double> page_item<PAGE_DIMENSION>::rotate(int my_angle)
  {
    angle -= my_angle;

    LOG_S(INFO) << "my_angle: " << my_angle;
    
    utils::values::rotate_inplace(my_angle, media_bbox);

    LOG_S(INFO) << "media: "
		<< media_bbox[0] << ", "
		<< media_bbox[1] << ", "
		<< media_bbox[2] << ", "
		<< media_bbox[3];
    
    utils::values::rotate_inplace(my_angle, crop_bbox);

    LOG_S(INFO) << "crop: "
		<< crop_bbox[0] << ", "
		<< crop_bbox[1] << ", "
		<< crop_bbox[2] << ", "
		<< crop_bbox[3];
	
    utils::values::rotate_inplace(my_angle, bleed_bbox);
    utils::values::rotate_inplace(my_angle, trim_bbox);
    utils::values::rotate_inplace(my_angle, art_bbox);

    utils::values::rotate_inplace(my_angle, bbox);
    
    std::pair<double, double> delta = {0.0, std::abs(media_bbox[3])};
    
    media_bbox[3] += 2*delta.second;
    crop_bbox[3] += 2*delta.second;
    bleed_bbox[3] += 2*delta.second;
    trim_bbox[3] += 2*delta.second;
    art_bbox[3] += 2*delta.second;
    
    bbox[3] += 2*delta.second;

    LOG_S(INFO) << "crop: "
		<< crop_bbox[0] << ", "
		<< crop_bbox[1] << ", "
		<< crop_bbox[2] << ", "
		<< crop_bbox[3];

    LOG_S(INFO) << "bbox: "
		<< bbox[0] << ", "
		<< bbox[1] << ", "
		<< bbox[2] << ", "
		<< bbox[3];
    
    return delta;
  }

  std::array<double, 4> page_item<PAGE_DIMENSION>::normalize_page_boundaries(std::array<double, 4> bbox, std::string name)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    double llx = std::min(bbox[0], bbox[2]);
    double lly = std::min(bbox[1], bbox[3]);
    double urx = std::max(bbox[0], bbox[2]);
    double ury = std::max(bbox[1], bbox[3]);

    if(urx<llx)
      {
	LOG_S(ERROR) << "we have a malformed page-boundary for " << name << "-> llx: "<< llx << ", urx: "<< urx;
      }

    if(ury<lly)
      {
	LOG_S(ERROR) << "we have a malformed page-boundary for " << name << "-> lly: "<< lly << ", ury: "<< ury;
      }    
    
    bbox[0] = llx;
    bbox[1] = lly;
    bbox[2] = urx;
    bbox[3] = ury;

    return bbox;
  }
  
  bool page_item<PAGE_DIMENSION>::init_from(nlohmann::json& data)
  {
    //LOG_S(INFO) << "reading: " << data.dump(2);
    LOG_S(INFO) << __FUNCTION__;
    
    if(data.count("bbox")==1 and
       data.count("angle")==1 and

       data.count("rectangles")==1 and

       data["rectangles"].count("media-bbox")==1 and
       data["rectangles"].count("crop-bbox")==1 and
       data["rectangles"].count("bleed-bbox")==1 and
       data["rectangles"].count("trim-bbox")==1 and
       data["rectangles"].count("art-bbox")==1)
      {
	bbox = data["bbox"].get<std::array<double, 4> >();
	angle = data["angle"].get<int>();
	
	media_bbox = data["rectangles"]["media-bbox"].get<std::array<double, 4> >();
	crop_bbox = data["rectangles"]["crop-bbox"].get<std::array<double, 4> >();
	bleed_bbox = data["rectangles"]["bleed-bbox"].get<std::array<double, 4> >();
	trim_bbox = data["rectangles"]["trim-bbox"].get<std::array<double, 4> >();
	art_bbox = data["rectangles"]["art-bbox"].get<std::array<double, 4> >();
	
	return true;
      }
    else
      {
	std::stringstream ss;
	ss << "could not read: " << data.dump(2);
	
	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }
    
    return false;
  }
  
  std::array<double, 4> page_item<PAGE_DIMENSION>::qpdf_bbox_to_array(QPDFObjectHandle qpdf_arr,
								       std::string name)
  {
    std::array<double, 4> result = {0, 0, 0, 0};

    if(not qpdf_arr.isArray())
      {
	LOG_S(WARNING) << name << " is not an array, skipping";
	return result;
      }

    int n = qpdf_arr.getArrayNItems();
    if(n != 4)
      {
	LOG_S(WARNING) << name << " has " << n << " items instead of 4";
      }

    for(int d = 0; d < 4 && d < n; d++)
      {
	QPDFObjectHandle item = qpdf_arr.getArrayItem(d);
	if(item.isNumber())
	  {
	    result[d] = item.getNumericValue();
	  }
	else
	  {
	    LOG_S(WARNING) << name << "[" << d << "] is not a number: " << item.unparse();
	    result[d] = 0;
	  }
      }

    return result;
  }

  // Table 30, p 85
  void page_item<PAGE_DIMENSION>::execute(QPDFObjectHandle qpdf_page)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(qpdf_page.hasKey("/Rotate"))
      {
        QPDFObjectHandle rotate_obj = qpdf_page.getKey("/Rotate");
	if(rotate_obj.isInteger())
	  {
	    angle = static_cast<int>(rotate_obj.getIntValue());
	    LOG_S(INFO) << "found a rotated page with angle: " << angle;
	  }
	else
	  {
	    LOG_S(WARNING) << "/Rotate is not an integer: " << rotate_obj.unparse();
	    angle = 0;
	  }
      }
    else
      {
        angle = 0;
      }

    if(qpdf_page.hasKey("/MediaBox"))
      {
        media_bbox = qpdf_bbox_to_array(qpdf_page.getKey("/MediaBox"), "/MediaBox");
      }
    // it might inherit the media-bbox from an ancestor in the page tree (sec 7.7.3.4, p 80)
    // PDF allows MediaBox to be inherited from any parent, not just the immediate parent
    else
      {
        bool found_mediabox = false;
        QPDFObjectHandle current = qpdf_page;

        // Traverse the parent chain to find inherited MediaBox
        // Limit depth to prevent infinite loops in malformed PDFs
        for(int depth = 0; depth < 10 && current.hasKey("/Parent"); depth++)
          {
            QPDFObjectHandle parent = current.getKey("/Parent");
            if(parent.hasKey("/MediaBox"))
              {
                media_bbox = qpdf_bbox_to_array(parent.getKey("/MediaBox"), "/MediaBox (inherited)");

                LOG_S(INFO) << "inherited MediaBox from ancestor at depth " << (depth + 1)
                            << ": [" << media_bbox[0] << ", " << media_bbox[1]
                            << ", " << media_bbox[2] << ", " << media_bbox[3] << "]";

                found_mediabox = true;
                break;
              }
            current = parent;
          }

        if(!found_mediabox)
          {
            LOG_S(ERROR) << "The page is missing the required '/MediaBox'";
          }
      }

    bool has_cropbox = qpdf_page.hasKey("/CropBox");
    bool has_bleedbox = qpdf_page.hasKey("/BleedBox");
    bool has_trimbox = qpdf_page.hasKey("/TrimBox");
    bool has_artbox = qpdf_page.hasKey("/ArtBox");

    if(has_cropbox)
      {
        crop_bbox = qpdf_bbox_to_array(qpdf_page.getKey("/CropBox"), "/CropBox");
      }
    else
      {
        crop_bbox = media_bbox;
      }

    if(crop_bbox[0]<media_bbox[0] or
       crop_bbox[2]>media_bbox[2] or
       crop_bbox[1]<media_bbox[1] or
       crop_bbox[3]>media_bbox[3])
      {
        LOG_S(ERROR) << "The crop-box is larger than the media-box, \n"
		     << "crop-box: {"
		     << crop_bbox[0] << ", "
		     << crop_bbox[1] << ", "
		     << crop_bbox[2] << ", "
		     << crop_bbox[3] << "}\n"
		     << "media-box: {"
		     << media_bbox[0] << ", "
		     << media_bbox[1] << ", "
		     << media_bbox[2] << ", "
		     << media_bbox[3] << "}\n";

	crop_bbox[0] = std::max(crop_bbox[0], media_bbox[0]);
	crop_bbox[1] = std::max(crop_bbox[1], media_bbox[1]);
	crop_bbox[2] = std::min(crop_bbox[2], media_bbox[2]);
	crop_bbox[3] = std::min(crop_bbox[3], media_bbox[3]);
      }

    if(has_bleedbox)
      {
        bleed_bbox = qpdf_bbox_to_array(qpdf_page.getKey("/BleedBox"), "/BleedBox");
      }
    else
      {
        bleed_bbox = crop_bbox;
      }

    if(has_trimbox)
      {
        trim_bbox = qpdf_bbox_to_array(qpdf_page.getKey("/TrimBox"), "/TrimBox");
      }
    else
      {
        trim_bbox = crop_bbox;
      }

    if(has_artbox)
      {
        art_bbox = qpdf_bbox_to_array(qpdf_page.getKey("/ArtBox"), "/ArtBox");
      }
    else
      {
        art_bbox = crop_bbox;
      }

    // FIXME: cleanup and review the box priorities
    if((not initialised) and has_cropbox)
      {
	std::stringstream ss;
	ss << "defaulting to crop-box";
        LOG_S(INFO) << ss.str();

        bbox = crop_bbox;
        initialised = true;
      }
    // Check if media_bbox was set (either directly or via inheritance)
    // media_bbox is initialized to {0,0,0,0}, so non-zero values indicate it was found
    else if((not initialised) and (media_bbox[2] > 0 || media_bbox[3] > 0))
      {
	std::stringstream ss;
	ss << "defaulting to media-box";
        LOG_S(INFO) << ss.str();

	crop_bbox = media_bbox;

        bbox = media_bbox;
        initialised = true;
      }
    else if((not initialised) and has_artbox)
      {
	std::stringstream ss;
	ss << "defaulting to art-box";
        LOG_S(INFO) << ss.str();

	crop_bbox = art_bbox;
	media_bbox = art_bbox;

        bbox = art_bbox;
        initialised = true;
      }
    else if((not initialised) and has_bleedbox)
      {
	std::stringstream ss;
	ss << "defaulting to bleed-box";
        LOG_S(INFO) << ss.str();

	crop_bbox = bleed_bbox;
	media_bbox = bleed_bbox;

        bbox = bleed_bbox;
        initialised = true;
      }
    else if((not initialised) and has_trimbox)
      {
	std::stringstream ss;
	ss << "defaulting to trim-box";
        LOG_S(INFO) << ss.str();

	crop_bbox = trim_bbox;
	media_bbox = trim_bbox;

        bbox = trim_bbox;
        initialised = true;
      }
    else
      {
	std::stringstream ss;
	ss << "could not find the page-dimensions";

        LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    LOG_S(INFO) << "crop-box: ("
		<< crop_bbox[0] << ", "
		<< crop_bbox[1] << ", "
		<< crop_bbox[2] << ", "
		<< crop_bbox[3] << ")";

    LOG_S(INFO) << "media-box: ("
		<< media_bbox[0] << ", "
		<< media_bbox[1] << ", "
		<< media_bbox[2] << ", "
		<< media_bbox[3] << ")";      

    LOG_S(INFO) << "art-box: ("
		<< art_bbox[0] << ", "
		<< art_bbox[1] << ", "
		<< art_bbox[2] << ", "
		<< art_bbox[3] << ")";

    LOG_S(INFO) << "bleed-box: ("
		<< bleed_bbox[0] << ", "
		<< bleed_bbox[1] << ", "
		<< bleed_bbox[2] << ", "
		<< bleed_bbox[3] << ")";

    LOG_S(INFO) << "trim-box: ("
		<< trim_bbox[0] << ", "
		<< trim_bbox[1] << ", "
		<< trim_bbox[2] << ", "
		<< trim_bbox[3] << ")";
    
    crop_bbox = normalize_page_boundaries(crop_bbox, "crop_bbox");
    media_bbox = normalize_page_boundaries(media_bbox, "media_bbox");
    art_bbox = normalize_page_boundaries(art_bbox, "art_bbox");
    bleed_bbox = normalize_page_boundaries(bleed_bbox, "bleed_bbox");
    trim_bbox = normalize_page_boundaries(trim_bbox, "trim_bbox");

    LOG_S(INFO) << "crop-box: ("
		<< crop_bbox[0] << ", "
		<< crop_bbox[1] << ", "
		<< crop_bbox[2] << ", "
		<< crop_bbox[3] << ")";

    LOG_S(INFO) << "media-box: ("
		<< media_bbox[0] << ", "
		<< media_bbox[1] << ", "
		<< media_bbox[2] << ", "
		<< media_bbox[3] << ")";      

    LOG_S(INFO) << "art-box: ("
		<< art_bbox[0] << ", "
		<< art_bbox[1] << ", "
		<< art_bbox[2] << ", "
		<< art_bbox[3] << ")";

    LOG_S(INFO) << "bleed-box: ("
		<< bleed_bbox[0] << ", "
		<< bleed_bbox[1] << ", "
		<< bleed_bbox[2] << ", "
		<< bleed_bbox[3] << ")";

    LOG_S(INFO) << "trim-box: ("
		<< trim_bbox[0] << ", "
		<< trim_bbox[1] << ", "
		<< trim_bbox[2] << ", "
		<< trim_bbox[3] << ")";    
  }

}

#endif
