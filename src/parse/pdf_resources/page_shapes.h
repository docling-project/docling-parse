//-*-C++-*-

#ifndef PDF_PAGE_SHAPES_RESOURCE_H
#define PDF_PAGE_SHAPES_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_SHAPES>
  {
    typedef typename std::vector<pdf_resource<PAGE_SHAPE> >::iterator itr_type;
    
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    void rotate(int angle, std::pair<double, double> delta);
    
    pdf_resource<PAGE_SHAPE>& operator[](size_t i);

    void clear();
    size_t size();

    pdf_resource<PAGE_SHAPE>& back();
    void push_back(pdf_resource<PAGE_SHAPE>& shape);

    itr_type begin() { return shapes.begin(); }
    itr_type end() { return shapes.end(); }
    
    itr_type erase(itr_type itr) { return shapes.erase(itr); }
    
  private:

    std::vector<pdf_resource<PAGE_SHAPE> > shapes;
  };

  pdf_resource<PAGE_SHAPES>::pdf_resource()
  {}

  pdf_resource<PAGE_SHAPES>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_SHAPES>::get()
  {
    nlohmann::json result = nlohmann::json::array();

    for(auto& shape:shapes)
      {
	if(shape.size()>0)
	  {
	    result.push_back(shape.get());
	  }
      }

    return result;
  }

  bool pdf_resource<PAGE_SHAPES>::init_from(nlohmann::json& data)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    bool result=true;
    
    if(data.is_array())
      {
	shapes.clear();
	shapes.resize(data.size());

	for(int i=0; i<data.size(); i++)	  
	  {
	    result = (result and shapes.at(i).init_from(data[i]));
	  }
      }
    else
      {
	std::stringstream ss;
	ss << "can not initialise pdf_resource<PAGE_SHAPES> from "
	   << data.dump(2);

	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());	
      }
    
    return result;
  }

  void pdf_resource<PAGE_SHAPES>::rotate(int angle, std::pair<double, double> delta)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& shape:shapes)
      {
	shape.rotate(angle, delta);
      }
  }
  
  pdf_resource<PAGE_SHAPE>& pdf_resource<PAGE_SHAPES>::operator[](size_t i)
  {    
    return shapes.at(i);
  }

  void pdf_resource<PAGE_SHAPES>::clear()
  {
    shapes.clear();
  }

  size_t pdf_resource<PAGE_SHAPES>::size()
  {
    return shapes.size();
  }

  pdf_resource<PAGE_SHAPE>& pdf_resource<PAGE_SHAPES>::back()
  {
    if(shapes.size()==0)
      {
	std::string message = "can not retrieve a shape, no shapes are known";
	LOG_S(ERROR) << message;
	throw std::logic_error(message);
      }
    
    return shapes.back();
  }

  void pdf_resource<PAGE_SHAPES>::push_back(pdf_resource<PAGE_SHAPE>& shape)
  {
    shapes.push_back(shape);
  }

}

#endif
