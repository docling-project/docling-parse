//-*-C++-*-

#ifndef PDF_PAGE_DIMENSION_SANITATOR_H
#define PDF_PAGE_DIMENSION_SANITATOR_H

namespace pdflib
{

  template<>
  class pdf_sanitator<PAGE_DIMENSION>
  {
  public:

    pdf_sanitator(pdf_resource<PAGE_DIMENSION>& page_dims);
    ~pdf_sanitator();

    void sanitize(std::string page_boundary);
    
    void sanitize(pdf_resource<PAGE_CELLS>& cells, std::string page_boundary);

    void sanitize(pdf_resource<PAGE_LINES>& lines, std::string page_boundary);

    void sanitize(pdf_resource<PAGE_IMAGES>& images, std::string page_boundary);

  private:

    std::array<double, 4> get_page_boundary(std::string page_boundary);
    
    void initialise();

    void transform(std::array<double, 2>& v,
		   std::array<double, 2>& r);

  private:

    pdf_resource<PAGE_DIMENSION>& page_dims;

    std::array<double, 4> crop_bbox;
    std::array<double, 4> media_bbox;

    std::array<std::array<double, 2>, 2> R;
  };
  
  pdf_sanitator<PAGE_DIMENSION>::pdf_sanitator(pdf_resource<PAGE_DIMENSION>& page_dims_):
    page_dims(page_dims_)
  {
    LOG_S(INFO) << __FUNCTION__;
    initialise();
  }
  
  pdf_sanitator<PAGE_DIMENSION>::~pdf_sanitator()
  {
    LOG_S(INFO) << __FUNCTION__;
  }

  void pdf_sanitator<PAGE_DIMENSION>::initialise()
  {
    LOG_S(INFO) << __FUNCTION__;

    crop_bbox = page_dims.get_crop_bbox();
    media_bbox = page_dims.get_media_bbox();

    /*
      for(int d0=0; d0<4; d0++)
      {
	LOG_S(INFO) << d0 << "\t" << D[d0];
      }
    */

    double pi    = 3.141592;
    double angle = page_dims.get_angle();

    R[0][0] =  std::cos(pi*angle/180.0);
    R[0][1] =  std::sin(pi*angle/180.0);
    R[1][0] = -std::sin(pi*angle/180.0);
    R[1][1] =  std::cos(pi*angle/180.0);

    /*
    for(int d0=0; d0<2; d0++)
      {
	for(int d1=0; d1<2; d1++)
	  {
	    LOG_S(INFO) << d0 << "\t" << d1 << "\t" << R[d0][d1];
	  }
      }
    */
  }

  std::array<double, 4> pdf_sanitator<PAGE_DIMENSION>::get_page_boundary(std::string page_boundary)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(page_boundary=="media_box")
      {
	return media_bbox;
      }
    else if(page_boundary=="crop_box")
      {
	return crop_bbox;
      }
    else
      {
	std::set<std::string> page_boundaries = {"media_box", "crop_box"};
	
	std::stringstream ss;

	ss << "page_boundary: " << page_boundary << " is not one of the allowed options. Please choose from [";
	for(auto _:page_boundaries)
	  {
	    ss << _ << ", ";
	  }
	ss << "] => falling back to crop_box!";

	LOG_S(ERROR) << ss.str();
      }

    return crop_bbox;
  }

  void pdf_sanitator<PAGE_DIMENSION>::sanitize(std::string page_boundary)
  {
    page_dims.set_page_boundaries(page_boundary);
  }
  
  void pdf_sanitator<PAGE_DIMENSION>::sanitize(pdf_resource<PAGE_CELLS>& cells, std::string page_boundary)
  {
    LOG_S(INFO) << __FUNCTION__;

    std::array<double, 4> page_bbox = get_page_boundary(page_boundary);
    
    // filter everything out
    for(auto itr=cells.begin(); itr!=cells.end(); )
      {
	std::array<double, 4> cell_bbox = {itr->x0, itr->y0, itr->x1, itr->y1};

	if(page_bbox[0]<=cell_bbox[0] and
	   page_bbox[1]<=cell_bbox[1] and
	   cell_bbox[2]<=page_bbox[2] and
	   cell_bbox[3]<=page_bbox[3])
	  {
	    itr->x0 -= page_bbox[0];
	    itr->y0 -= page_bbox[1];

	    itr->x1 -= page_bbox[0];
	    itr->y1 -= page_bbox[1];

	    itr->r_x0 -= page_bbox[0];
	    itr->r_y0 -= page_bbox[1];

	    itr->r_x1 -= page_bbox[0];
	    itr->r_y1 -= page_bbox[1];

	    itr->r_x2 -= page_bbox[0];
	    itr->r_y2 -= page_bbox[1];

	    itr->r_x3 -= page_bbox[0];
	    itr->r_y3 -= page_bbox[1];	    	    
	    
	    itr++;
	  }
	else
	  {
	    LOG_S(INFO) << "deleting cell: ("
			<< itr->x0 << ", " << itr->y0 << ", " << itr->x1 << ", " << itr->y1 << ")";
	      
	    itr = cells.erase(itr);
	  }
      }

    // I might need it later for rotated pages ...
    
    /*
    std::array<double, 2> r;
    for(size_t l=0; l<cells.size(); l++)
      {
	pdf_resource<PAGE_CELL>& cell = cells[l];
	
	std::array<std::array<double, 2>, 6> vecs = {
	  std::array<double, 2>({cell.x0, cell.y0}),
	  std::array<double, 2>({cell.x1, cell.y1}),

	  std::array<double, 2>({cell.r_x0, cell.r_y0}),
	  std::array<double, 2>({cell.r_x1, cell.r_y1}),
	  std::array<double, 2>({cell.r_x2, cell.r_y2}),
	  std::array<double, 2>({cell.r_x3, cell.r_y3})
	};

	for(int l=0; l<vecs.size(); l++)
	  {
	    transform(vecs[l], r);
	  }

	//LOG_S(INFO) << cell.x0    << ", " << cell.y0 << "  ->  " 
	//	    << vecs[0][0] << ", " << vecs[0][1]; 

	{
	  cell.x0 = vecs[0][0];
	  cell.y0 = vecs[0][1];

	  cell.x1 = vecs[1][0];
	  cell.y1 = vecs[1][1];

	  cell.r_x0 = vecs[2][0];
	  cell.r_y0 = vecs[2][1];

	  cell.r_x1 = vecs[3][0];
	  cell.r_y1 = vecs[3][1];

	  cell.r_x2 = vecs[4][0];
	  cell.r_y2 = vecs[4][1];

	  cell.r_x3 = vecs[5][0];
	  cell.r_y3 = vecs[5][1];
	}
      }
    */    
  }

  void pdf_sanitator<PAGE_DIMENSION>::sanitize(pdf_resource<PAGE_LINES>& lines, std::string page_boundary)
  {
    LOG_S(INFO) << __FUNCTION__;

    std::array<double, 4> page_bbox = get_page_boundary(page_boundary);
    
    // filter everything out
    for(auto itr=lines.begin(); itr!=lines.end(); )
      {
	pdf_resource<PAGE_LINE>& line = *itr;

	std::vector<double>& x = line.get_x();
	std::vector<double>& y = line.get_y();

	bool is_contained_in_boundary=true;
	for(std::size_t l=0; l<x.size(); l++)
	  {
	    double rx = x.at(l);
	    double ry = y.at(l);
	    
	    if(page_bbox[0]<=rx and rx<=page_bbox[2] and
	       page_bbox[1]<=ry and ry<=page_bbox[3])
	      {}
	    else
	      {
		is_contained_in_boundary=true;
	      }
	  }
	
	if(is_contained_in_boundary)
	  {
	    for(std::size_t l=0; l<x.size(); l++)
	      {
		x.at(l) -= page_bbox[0];
		y.at(l) -= page_bbox[1];
	      }
	    
	    itr++;	    
	  }
	else
	  {
	    itr = lines.erase(itr);	    
	  }
      }

    // I might need it later for rotated pages ...
    
    /*
    std::array<double, 2> v, r;

    for(size_t l=0; l<lines.size(); l++)
      {
	pdf_resource<PAGE_LINE>& line = lines[l];

	std::vector<double>& x = line.get_x();
	std::vector<double>& y = line.get_y();

	for(size_t k=0; k<x.size(); k++)
	  {
	    v = {x[k], y[k]};
	    
	    transform(v, r);

	    x[k] = v[0];
	    y[k] = v[1];
	  }
      }
    */
  }
  
  void pdf_sanitator<PAGE_DIMENSION>::sanitize(pdf_resource<PAGE_IMAGES>& images, std::string page_boundary)
  {
    LOG_S(INFO) << __FUNCTION__;

    std::array<double, 4> page_bbox = get_page_boundary(page_boundary);
    
    // filter everything out
    for(auto itr=images.begin(); itr!=images.end(); )
      {
	std::array<double, 4> image_bbox = {itr->x0, itr->y0, itr->x1, itr->y1};

	// Calculate the intersection rectangle
	double x0 = std::max(page_bbox[0], image_bbox[0]); // Max of bottom-left x
	double y0 = std::max(page_bbox[1], image_bbox[1]); // Max of bottom-left y
	double x1 = std::min(page_bbox[2], image_bbox[2]); // Min of top-right x
	double y1 = std::min(page_bbox[3], image_bbox[3]); // Min of top-right y
	
	// Validate the intersection
	if (x0 > x1 || y0 > y1)
	  {
	    LOG_S(INFO) << "deleting image: ("
			<< itr->x0 << ", " << itr->y0 << ", " << itr->x1 << ", " << itr->y1 << ")";
	    
	    // No intersection, remove
	    itr = images.erase(itr);
	  }
	else
	  {
	    itr->x0 = x0 - page_bbox[0];
	    itr->y0 = y0 - page_bbox[1];
	    itr->x1 = x1 - page_bbox[0];
	    itr->y1 = y1 - page_bbox[1];

	    LOG_S(INFO) << "keeping image: ("
			<< itr->x0 << ", " << itr->y0 << ", " << itr->x1 << ", " << itr->y1 << ")";
	    
	    itr++;	    
	  }
	
	/*
	if(page_bbox[0]<=image_bbox[0] and
	   page_bbox[1]<=image_bbox[1] and
	   image_bbox[2]<=page_bbox[2] and
	   image_bbox[3]<=page_bbox[3])
	  {
	    itr->x0 -= page_bbox[0];
	    itr->y0 -= page_bbox[1];
	    itr->x1 -= page_bbox[0];
	    itr->y1 -= page_bbox[1];
	    
	    itr++;
	  }
	else
	  {

	  }
	*/
      }

    // I might need it later for rotated pages ...
    
    /*
    std::array<double, 2> r;

    for(size_t l=0; l<images.size(); l++)
      {
	pdf_resource<PAGE_IMAGE>& image = images[l];

	std::array<std::array<double, 2>, 6> vecs = {
	  std::array<double, 2>({image.x0, image.y0}),
	  std::array<double, 2>({image.x1, image.y1})
	};

	for(int l=0; l<vecs.size(); l++)
	  {
	    transform(vecs[l], r);
	  }

	//LOG_S(INFO) << image.x0    << ", " << image.y0 << "  ->  " 
	//	    << vecs[0][0] << ", " << vecs[0][1]; 

	{
	  image.x0 = vecs[0][0];
	  image.y0 = vecs[0][1];

	  image.x1 = vecs[1][0];
	  image.y1 = vecs[1][1];
	}
      }
    */
  }

  void pdf_sanitator<PAGE_DIMENSION>::transform(std::array<double, 2>& v,
						std::array<double, 2>& r)
  {
    /*
    std::array<double, 4>& b = media_bbox;
    std::array<double, 4>& B = crop_bbox;

    r[0] = (v[0]-b[0])/(b[2]-b[0]) - 0.5;
    r[1] = (v[1]-b[1])/(b[3]-b[1]) - 0.5;

    v[0] = R[0][0]*r[0] + R[0][1]*r[1];
    v[1] = R[1][0]*r[0] + R[1][1]*r[1];

    v[0] = (B[2]-B[0])*(v[0]+0.5);// + D[0];
    v[1] = (B[3]-B[1])*(v[1]+0.5);// + D[1];
    */
    
    // FIXME    
    //v = r;
  }

}

#endif
