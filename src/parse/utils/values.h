//-*-C++-*-

#ifndef PDF_UTILS_VALUES_H
#define PDF_UTILS_VALUES_H

namespace utils
{
  namespace values
  {
    double round(const double& v)
    {
      int t = std::round(1000.0*v);
      return static_cast<double>(t)/1000.0;
    }

    // intersection / area of _1
    double compute_overlap(double x0_1, double y0_1, double x1_1, double y1_1,
			   double x0_2, double y0_2, double x1_2, double y1_2)
    {
      // Compute the coordinates of the intersection rectangle
      double x0_inter = std::max(x0_1, x0_2);
      double y0_inter = std::max(y0_1, y0_2);
      double x1_inter = std::min(x1_1, x1_2);
      double y1_inter = std::min(y1_1, y1_2);

      // Compute the width and height of the intersection rectangle
      double width_inter = std::max(0.0, x1_inter - x0_inter);
      double height_inter = std::max(0.0, y1_inter - y0_inter);
      
      // Compute the area of the intersection rectangle
      double area_inter = width_inter * height_inter;
      
      // Compute the area of both rectangles
      double area_rect1 = (x1_1 - x0_1) * (y1_1 - y0_1);
      //double area_rect2 = (x1_2 - x0_2) * (y1_2 - y0_2);
      
      // Compute the union area
      //double area_union = area_rect1 + area_rect2 - area_inter;
      
      // Compute IoU
      //return (area_union > 0) ? (area_inter / area_union) : 0.0;
      return (area_rect1 > 1.e-3) ? (area_inter / area_rect1) : 0.0;
    }
    
    double compute_IoU(double x0_1, double y0_1, double x1_1, double y1_1,
		       double x0_2, double y0_2, double x1_2, double y1_2)
    {
      // Compute the coordinates of the intersection rectangle
      double x0_inter = std::max(x0_1, x0_2);
      double y0_inter = std::max(y0_1, y0_2);
      double x1_inter = std::min(x1_1, x1_2);
      double y1_inter = std::min(y1_1, y1_2);

      // Compute the width and height of the intersection rectangle
      double width_inter = std::max(0.0, x1_inter - x0_inter);
      double height_inter = std::max(0.0, y1_inter - y0_inter);
      
      // Compute the area of the intersection rectangle
      double area_inter = width_inter * height_inter;
      
      // Compute the area of both rectangles
      double area_rect1 = (x1_1 - x0_1) * (y1_1 - y0_1);
      double area_rect2 = (x1_2 - x0_2) * (y1_2 - y0_2);
      
      // Compute the union area
      double area_union = area_rect1 + area_rect2 - area_inter;
      
      // Compute IoU
      return (area_union > 0) ? (area_inter / area_union) : 0.0;
    }

    double distance(double x0, double y0, double x1, double y1)
    {
      return std::sqrt(std::pow(x0-x1, 2)+std::pow(y0-y1, 2));
    }
    
    void rotate_inplace(int angle, double& x0, double& y0)
    {
      //double phi = -3.141592*angle/180.0;
      double phi = -M_PI*angle/180.0;
      
      double x1 = x0, y1 = y0;

      x0 = std::cos(phi)*x1 - std::sin(phi)*y1;
      y0 = std::sin(phi)*x1 + std::cos(phi)*y1;
    }

    void translate_inplace(std::pair<double, double> delta, double& x0, double& y0)
    {
      x0 += delta.first;
      y0 += delta.second;
    }    
    
    void rotate_inplace(int angle, std::array<double, 4>& bbox)
    {
      double phi = -3.141592*angle/180.0;
      
      std::array<double, 4> tmp = bbox;

      bbox[0] = std::cos(phi)*tmp[0] - std::sin(phi)*tmp[1];
      bbox[1] = std::sin(phi)*tmp[0] + std::cos(phi)*tmp[1];

      bbox[2] = std::cos(phi)*tmp[2] - std::sin(phi)*tmp[3];
      bbox[3] = std::sin(phi)*tmp[2] + std::cos(phi)*tmp[3];
    }

    void transform_bottomleft_bbox_inplace(const int& angle,
					   const std::pair<double, double>& delta,
					   std::array<double, 4>& bbox)
    {
      // LOG_S(INFO) << "delta-x1: " << delta.first << ", delta-y1: " << delta.second;
      
      double& x0 = bbox.at(0);
      double& y0 = bbox.at(1);

      double& x1 = bbox.at(2);
      double& y1 = bbox.at(3);

      // LOG_S(INFO) << "before x1: " << x1 << ", y1: " << y1;
      
      utils::values::rotate_inplace(angle, x0, y0);
      utils::values::rotate_inplace(angle, x1, y1);

      // LOG_S(INFO) << "rotate  x1: " << x1 << ", y1: " << y1;
      
      utils::values::translate_inplace(delta, x0, y0);
      utils::values::translate_inplace(delta, x1, y1);

      // LOG_S(INFO) << "translate x1: " << x1 << ", y1: " << y1;
      
      // The bounding box always needs to have x0<x1 and y0<y1. If you want
      // to keep the orientation of the image, we will need to add the rectangle
      // as demonstrated in the page_cell.
    
      double x_min = std::min(x0, x1);
      double x_max = std::max(x0, x1);
      
      double y_min = std::min(y0, y1);
      double y_max = std::max(y0, y1);

      x0 = x_min;
      x1 = x_max;
      
      y0 = y_min;
      y1 = y_max;      
    }
    
  }
  
}

#endif
