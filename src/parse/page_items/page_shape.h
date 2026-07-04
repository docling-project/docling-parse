//-*-C++-*-

#ifndef PAGE_ITEM_SHAPE_H
#define PAGE_ITEM_SHAPE_H

#include <parse/enums.h>

namespace pdflib
{
  
  template<>
  class page_item<PAGE_SHAPE>
  {
  public:

    page_item();
    ~page_item();

    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    void rotate(int angle, std::pair<double, double> delta);
    
    std::vector<int>& get_i() { return i; }    

    std::vector<double>& get_x() { return x; }
    std::vector<double>& get_y() { return y; }

    void set_graphics_state(double line_width_, double miter_limit_,
                            int line_cap_, int line_join_,
                            double dash_phase_, const std::vector<double>& dash_array_,
                            double flatness_,
                            const std::array<int, 3>& rgb_stroking_ops_,
                            const std::array<int, 3>& rgb_filling_ops_);

    page_shape_closing_type get_closing_type() const { return closing_type; }
    page_shape_type         get_shape_type()   const { return shape_type; }

    void set_closing_type(page_shape_closing_type ct) { closing_type = ct; }
    void set_shape_type(page_shape_type st)           { shape_type   = st; }

    void append(double x_, double y_);

    // Dual-track geometry: `append` only extends the flattened polyline
    // (JSON output, bbox/degeneracy checks); the methods below additionally
    // record the exact segment structure so the renderer can rebuild true
    // curves. The subpath start (move-to) is the first plain `append`;
    // closing edges are expressed via closing_type, not as a segment.
    void append_line_segment(double x_, double y_);
    void append_cubic_segment(double x1_, double y1_,
                              double x2_, double y2_,
                              double x3_, double y3_);

    const std::vector<shape_segment_op>& get_seg_ops() const { return seg_ops; }
    const std::vector<double>& get_seg_x() const { return seg_x; }
    const std::vector<double>& get_seg_y() const { return seg_y; }

    size_t size();

    std::pair<double, double> front();
    std::pair<double, double> back();

    std::pair<double, double> operator[](int i);

    void transform(std::array<double, 9> trafo_matrix);

    bool get_has_graphics_state() const { return has_graphics_state; }

    double get_line_width() const { return line_width; }
    double get_miter_limit() const { return miter_limit; }
    int get_line_cap() const { return line_cap; }
    int get_line_join() const { return line_join; }
    double get_dash_phase() const { return dash_phase; }
    const std::vector<double>& get_dash_array() const { return dash_array; }
    double get_flatness() const { return flatness; }
    const std::array<int, 3>& get_rgb_stroking_ops() const { return rgb_stroking_ops; }
    const std::array<int, 3>& get_rgb_filling_ops() const { return rgb_filling_ops; }

  private:

    std::vector<int>    i;
    std::vector<double> x;
    std::vector<double> y;

    // exact segment structure (not serialized; render-instruction path only)
    std::vector<shape_segment_op> seg_ops;
    std::vector<double>           seg_x; // op-consumed points,
    std::vector<double>           seg_y; // in op order

    // graphics state properties
    bool has_graphics_state = false;

    double line_width = -1;
    double miter_limit = -1;

    int line_cap = -1;
    int line_join = -1;

    double              dash_phase = 0;
    std::vector<double> dash_array = {};

    double flatness = -1;

    std::array<int, 3> rgb_stroking_ops = {0, 0, 0};
    std::array<int, 3> rgb_filling_ops  = {0, 0, 0};

    page_shape_closing_type closing_type = CLOSING_UNDEFINED;
    page_shape_type         shape_type   = SHAPE_UNDEFINED;
  };

  page_item<PAGE_SHAPE>::page_item()
  {
    i = {0, 0};
    x = {};
    y = {};
  }

  page_item<PAGE_SHAPE>::~page_item()
  {}

  void page_item<PAGE_SHAPE>::set_graphics_state(double line_width_, double miter_limit_,
                                                    int line_cap_, int line_join_,
                                                    double dash_phase_, const std::vector<double>& dash_array_,
                                                    double flatness_,
                                                    const std::array<int, 3>& rgb_stroking_ops_,
                                                    const std::array<int, 3>& rgb_filling_ops_)
  {
    has_graphics_state = true;

    line_width  = line_width_;
    miter_limit = miter_limit_;

    line_cap  = line_cap_;
    line_join = line_join_;

    dash_phase = dash_phase_;
    dash_array = dash_array_;

    flatness = flatness_;

    rgb_stroking_ops = rgb_stroking_ops_;
    rgb_filling_ops  = rgb_filling_ops_;
  }

  nlohmann::json page_item<PAGE_SHAPE>::get()
  {
    for(size_t l=0; l<this->size(); l++)
      {
	x[l] = utils::values::round(x[l]);
	y[l] = utils::values::round(y[l]);
      }

    nlohmann::json result;
    {
      result["x"] = x;
      result["y"] = y;
      result["i"] = i;

      result["has-graphics-state"] = has_graphics_state;

      result["line-width"]  = utils::values::round(line_width);
      result["miter-limit"] = utils::values::round(miter_limit);

      result["line-cap"]  = line_cap;
      result["line-join"] = line_join;

      result["dash-phase"] = utils::values::round(dash_phase);
      result["dash-array"] = dash_array;

      result["flatness"] = utils::values::round(flatness);

      result["rgb-stroking"] = rgb_stroking_ops;
      result["rgb-filling"]  = rgb_filling_ops;

      result["closing-type"] = static_cast<int>(closing_type);
      result["shape-type"]   = static_cast<int>(shape_type);
    }
    return result;
  }

  bool page_item<PAGE_SHAPE>::init_from(nlohmann::json& data)
  {
    if(data.count("x")==1 and
       data.count("y")==1 and
       data.count("i")==1)
      {
	x = data["x"].get<std::vector<double> >();
	y = data["y"].get<std::vector<double> >();
	i = data["i"].get<std::vector<int> >();

	if(data.count("has-graphics-state")) { has_graphics_state = data["has-graphics-state"].get<bool>(); }

	if(data.count("line-width"))  { line_width  = data["line-width"].get<double>(); }
	if(data.count("miter-limit")) { miter_limit = data["miter-limit"].get<double>(); }

	if(data.count("line-cap"))  { line_cap  = data["line-cap"].get<int>(); }
	if(data.count("line-join")) { line_join = data["line-join"].get<int>(); }

	if(data.count("dash-phase")) { dash_phase = data["dash-phase"].get<double>(); }
	if(data.count("dash-array")) { dash_array = data["dash-array"].get<std::vector<double> >(); }

	if(data.count("flatness")) { flatness = data["flatness"].get<double>(); }

	if(data.count("rgb-stroking")) { rgb_stroking_ops = data["rgb-stroking"].get<std::array<int, 3> >(); }
	if(data.count("rgb-filling"))  { rgb_filling_ops  = data["rgb-filling"].get<std::array<int, 3> >(); }

	if(data.count("closing-type")) { closing_type = static_cast<page_shape_closing_type>(data["closing-type"].get<int>()); }
	if(data.count("shape-type"))   { shape_type   = static_cast<page_shape_type>(data["shape-type"].get<int>()); }

	return true;
      }
    else
      {
	LOG_S(WARNING) << "did not contain `x`, `y` or `i` in data: "
		       << data.dump(2);	
      }
    
    return false;
  }

  void page_item<PAGE_SHAPE>::rotate(int angle, std::pair<double, double> delta)
  {
    for(int l=0; l<x.size(); l++)
      {
	utils::values::rotate_inplace(angle, x.at(l), y.at(l));
	utils::values::translate_inplace(delta, x.at(l), y.at(l));
      }

    // affine maps transform Bézier control points exactly, so the segment
    // track follows the same point-wise mapping as the polyline
    for(size_t l=0; l<seg_x.size(); l++)
      {
	utils::values::rotate_inplace(angle, seg_x.at(l), seg_y.at(l));
	utils::values::translate_inplace(delta, seg_x.at(l), seg_y.at(l));
      }
  }
  
  void page_item<PAGE_SHAPE>::append(double x_, double y_)
  {
    x.push_back(x_);
    y.push_back(y_);

    i.back() += 1;
  }

  void page_item<PAGE_SHAPE>::append_line_segment(double x_, double y_)
  {
    // the first point of a subpath is the move-to, not a segment
    if(not x.empty())
      {
        seg_ops.push_back(SEGMENT_LINE_TO);
        seg_x.push_back(x_);
        seg_y.push_back(y_);
      }

    this->append(x_, y_);
  }

  void page_item<PAGE_SHAPE>::append_cubic_segment(double x1_, double y1_,
                                                   double x2_, double y2_,
                                                   double x3_, double y3_)
  {
    // records only the exact curve; the caller appends the flattened
    // samples through `append` (interpolate) to keep the polyline intact
    seg_ops.push_back(SEGMENT_CUBIC_TO);

    seg_x.push_back(x1_);
    seg_y.push_back(y1_);

    seg_x.push_back(x2_);
    seg_y.push_back(y2_);

    seg_x.push_back(x3_);
    seg_y.push_back(y3_);
  }

  size_t page_item<PAGE_SHAPE>::size()
  {
    return x.size();
  }

  std::pair<double, double> page_item<PAGE_SHAPE>::front()
  {
    //assert(x.size()>0);
    if(x.size()==0)
      {
	LOG_S(ERROR) << "applying front on empty page_shape ...";
	return std::make_pair(0, 0);
      }
    
    //return std::pair<double, double>(x.front(), y.front());
    return std::make_pair(x.front(), y.front());
  }

  std::pair<double, double> page_item<PAGE_SHAPE>::back()
  {
    //assert(x.size()>0);
    if(x.size()==0)
      {
	LOG_S(ERROR) << "applying front on empty page_shape ...";
	return std::make_pair(0, 0);
      }
    
    //return std::pair<double, double>(x.back(), y.back());
    return std::make_pair(x.back(), y.back());
  }

  std::pair<double, double> page_item<PAGE_SHAPE>::operator[](int i)
  {
    //assert(x.size()>0 and i<x.size());
    if(0<=i and i>=x.size())
      {
	LOG_S(ERROR) << "out of bounds index " << i << " for page-shape of size " << x.size();
	return std::make_pair(0, 0);	
      }
    
    //return std::pair<double, double>(x[i], y[i]);
    return std::make_pair(x[i], y[i]);
  }

  void page_item<PAGE_SHAPE>::transform(std::array<double, 9> trafo_matrix)
  {
    //assert(x.size()==y.size());
    if(x.size()!=y.size())
      {
	LOG_S(ERROR) << "inconsistent sizes between x: " << x.size() << " and y: " << y.size();
	return;
      }
    
    std::vector<double> x_, y_;

    for(size_t j=0; j<x.size(); j++)
      {
        std::array<double, 3> u = {x[j], y[j], 1.0};
        std::array<double, 3> d = {0.0 , 0.0 , 0.0};

        // p 120
        for(int j=0; j<3; j++)
          {
            for(int i=0; i<3; i++)
              {
                d[j] += u[i]*trafo_matrix[i*3+j];
              }
          }

        x_.push_back(d[0]);
        y_.push_back(d[1]);
      }

    x = x_;
    y = y_;

    // affine maps transform Bézier control points exactly, so the segment
    // track follows the same point-wise mapping as the polyline
    for(size_t l=0; l<seg_x.size(); l++)
      {
        std::array<double, 3> u = {seg_x[l], seg_y[l], 1.0};
        std::array<double, 3> d = {0.0, 0.0, 0.0};

        for(int j=0; j<3; j++)
          {
            for(int i=0; i<3; i++)
              {
                d[j] += u[i]*trafo_matrix[i*3+j];
              }
          }

        seg_x[l] = d[0];
        seg_y[l] = d[1];
      }
  }
  
}

#endif
