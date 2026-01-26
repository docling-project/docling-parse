//-*-C++-*-

#ifndef PDF_PAGE_DIMENSION_RESOURCE_H
#define PDF_PAGE_DIMENSION_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_DIMENSION>
  {
  public:

    pdf_resource();
    ~pdf_resource();
    
    void set_page_boundaries(std::string page_boundary);
    
    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    int get_angle() { return angle; }

    std::array<double, 4> get_crop_bbox() { return crop_bbox; }
    std::array<double, 4> get_media_bbox() { return media_bbox; }

    void execute(nlohmann::json& json_resources,
		 QPDFObjectHandle qpdf_resources);

    std::pair<double, double> rotate(int angle);
    
  private:

    std::array<double, 4> normalize_page_boundaries(std::array<double, 4> bbox, std::string name);
    
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

  pdf_resource<PAGE_DIMENSION>::pdf_resource():
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

  pdf_resource<PAGE_DIMENSION>::~pdf_resource()
  {}

  void pdf_resource<PAGE_DIMENSION>::set_page_boundaries(std::string page_boundary_)
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
  
  nlohmann::json pdf_resource<PAGE_DIMENSION>::get()
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

  std::pair<double, double> pdf_resource<PAGE_DIMENSION>::rotate(int my_angle)
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

  std::array<double, 4> pdf_resource<PAGE_DIMENSION>::normalize_page_boundaries(std::array<double, 4> bbox, std::string name)
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
  
  bool pdf_resource<PAGE_DIMENSION>::init_from(nlohmann::json& data)
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
  
  // Table 30, p 85
  void pdf_resource<PAGE_DIMENSION>::execute(nlohmann::json& json_resources,
					     QPDFObjectHandle qpdf_resources)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << json_resources.dump(2);

    if(json_resources.count("/Rotate"))
      {
        angle = json_resources["/Rotate"].get<int>();
	LOG_S(INFO) << "found a rotated poge with angle: " << angle;
      }
    else
      {
        angle = 0;
      }

    if(json_resources.count("/MediaBox"))
      {        
        for(int d=0; d<4; d++)
          {
            media_bbox[d] = json_resources["/MediaBox"][d].get<double>();
          }
      }
    // it might inherit the media-bbox from the parent document (sec 7.7.3.4, p 80)
    else if(qpdf_resources.hasKey("/Parent") and qpdf_resources.getKey("/Parent").hasKey("/MediaBox"))
      {
	QPDFObjectHandle qpdf_bbox = qpdf_resources.getKey("/Parent").getKey("/MediaBox"); 
	nlohmann::json   json_bbox = to_json(qpdf_bbox);

	//LOG_S(WARNING) << "inherited bbox: " << json_bbox.dump(2);	
	for(int d=0; d<4; d++)
          {
            media_bbox[d] = json_bbox[d].get<double>();
          }
      }
    else
      {
        LOG_S(ERROR) << "The page is missing the required '/MediaBox'";
      }

    if(json_resources.count("/CropBox"))
      {        
        for(int d=0; d<4; d++)
          {
            crop_bbox[d] = json_resources["/CropBox"][d].get<double>();
          }
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
    
    if(json_resources.count("/BleedBox"))
      {        
        for(int d=0; d<4; d++)
          {
            bleed_bbox[d] = json_resources["/BleedBox"][d].get<double>();
          }
      }
    else
      {
        bleed_bbox = crop_bbox;
      }

    if(json_resources.count("/TrimBox"))
      {        
        for(int d=0; d<4; d++)
          {
            trim_bbox[d] = json_resources["/TrimBox"][d].get<double>();
          }
      }
    else
      {
        trim_bbox = crop_bbox;
      }

    if(json_resources.count("/ArtBox"))
      {        
        for(int d=0; d<4; d++)
          {
            art_bbox[d] = json_resources["/ArtBox"][d].get<double>();
          }
      }
    else
      {
        art_bbox = crop_bbox;
      }
    
    // FIXME: cleanup and review the box priorities
    if((not initialised) and json_resources.count("/CropBox"))
      {
	std::stringstream ss;
	ss << "defaulting to crop-box";	
        LOG_S(INFO) << ss.str();
	
        bbox = crop_bbox;
        initialised = true;
      }    
    //else if(not initialised)
    else if((not initialised) and (json_resources.count("/MediaBox") or (qpdf_resources.hasKey("/Parent") and qpdf_resources.getKey("/Parent").hasKey("/MediaBox"))))
      {
	std::stringstream ss;
	ss << "defaulting to media-box";	
        LOG_S(INFO) << ss.str();

	crop_bbox = media_bbox;
	
        bbox = media_bbox;
        initialised = true;
      }
    else if((not initialised) and json_resources.count("/ArtBox"))
      {
	std::stringstream ss;
	ss << "defaulting to art-box";	
        LOG_S(INFO) << ss.str();

	crop_bbox = art_bbox;
	media_bbox = art_bbox;
	
        bbox = art_bbox;
        initialised = true;
      }    
    else if((not initialised) and json_resources.count("/BleedBox"))
      {
	std::stringstream ss;
	ss << "defaulting to bleed-box";	
        LOG_S(INFO) << ss.str();
	
	crop_bbox = bleed_bbox;
	media_bbox = bleed_bbox;
	
        bbox = bleed_bbox;
        initialised = true;
      }
    else if((not initialised) and json_resources.count("/TrimBox"))
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
	ss << "could not find the page-dimensions: " 
	   << json_resources.dump(4);
	
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
