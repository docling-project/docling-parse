//-*-C++-*-

#ifndef PDF_PAGE_LINE_RESOURCE_H
#define PDF_PAGE_LINE_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_LINE>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    void rotate(int angle, std::pair<double, double> delta);
    
    std::vector<int>& get_i() { return i; }    

    std::vector<double>& get_x() { return x; }
    std::vector<double>& get_y() { return y; }

    void append(double x_, double y_);

    size_t size();

    std::pair<double, double> front();
    std::pair<double, double> back();

    std::pair<double, double> operator[](int i);

    void transform(std::array<double, 9> trafo_matrix);
    
  private:

    std::vector<int>    i;    
    std::vector<double> x;
    std::vector<double> y;
  };

  pdf_resource<PAGE_LINE>::pdf_resource()
  {
    i = {0, 0};
    x = {};
    y = {};
  }

  pdf_resource<PAGE_LINE>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_LINE>::get()
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
    }
    return result;
  }

  bool pdf_resource<PAGE_LINE>::init_from(nlohmann::json& data)
  {
    if(data.count("x")==1 and
       data.count("y")==1 and
       data.count("i")==1)
      {
	x = data["x"].get<std::vector<double> >();
	y = data["y"].get<std::vector<double> >();
	i = data["i"].get<std::vector<int> >();
	
	return true;
      }
    else
      {
	LOG_S(WARNING) << "did not contain `x`, `y` or `i` in data: "
		       << data.dump(2);	
      }
    
    return false;
  }

  void pdf_resource<PAGE_LINE>::rotate(int angle, std::pair<double, double> delta)
  {
    for(int l=0; l<x.size(); l++)
      {
	utils::values::rotate_inplace(angle, x.at(l), y.at(l));
	utils::values::translate_inplace(delta, x.at(l), y.at(l));
      }
  }
  
  void pdf_resource<PAGE_LINE>::append(double x_, double y_)
  {
    x.push_back(x_);
    y.push_back(y_);

    i.back() += 1;
  }

  size_t pdf_resource<PAGE_LINE>::size()
  {
    return x.size();
  }

  std::pair<double, double> pdf_resource<PAGE_LINE>::front()
  {
    //assert(x.size()>0);
    if(x.size()==0)
      {
	LOG_S(ERROR) << "applying front on empty page_line ...";
	return std::make_pair(0, 0);
      }
    
    //return std::pair<double, double>(x.front(), y.front());
    return std::make_pair(x.front(), y.front());
  }

  std::pair<double, double> pdf_resource<PAGE_LINE>::back()
  {
    //assert(x.size()>0);
    if(x.size()==0)
      {
	LOG_S(ERROR) << "applying front on empty page_line ...";
	return std::make_pair(0, 0);
      }
    
    //return std::pair<double, double>(x.back(), y.back());
    return std::make_pair(x.back(), y.back());
  }

  std::pair<double, double> pdf_resource<PAGE_LINE>::operator[](int i)
  {
    //assert(x.size()>0 and i<x.size());
    if(0<=i and i>=x.size())
      {
	LOG_S(ERROR) << "out of bounds index " << i << " for page-line of size " << x.size();
	return std::make_pair(0, 0);	
      }
    
    //return std::pair<double, double>(x[i], y[i]);
    return std::make_pair(x[i], y[i]);
  }

  void pdf_resource<PAGE_LINE>::transform(std::array<double, 9> trafo_matrix)
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
            //LOG_S(WARNING) << trafo_matrix[0*3+j] << "\t" << trafo_matrix[1*3+j] << "\t" << trafo_matrix[2*3+j];

            for(int i=0; i<3; i++)
              {
                d[j] += u[i]*trafo_matrix[i*3+j];
              }

            //LOG_S(WARNING) << x[0] << "\t" << y[0] << "\t -> \t" << d[0] << "\t" << d[1];
          }

        x_.push_back(d[0]);
        y_.push_back(d[1]);
      }
    
    x = x_;
    y = y_;
  }
  
}

#endif
