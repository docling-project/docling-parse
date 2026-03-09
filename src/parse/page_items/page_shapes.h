//-*-C++-*-

#ifndef PAGE_ITEM_SHAPES_H
#define PAGE_ITEM_SHAPES_H

namespace pdflib
{

  template<>
  class page_item<PAGE_SHAPES>
  {
    typedef typename std::vector<page_item<PAGE_SHAPE> >::iterator itr_type;
    
  public:

    page_item();
    ~page_item();

    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    void rotate(int angle, std::pair<double, double> delta);
    
    page_item<PAGE_SHAPE>& operator[](size_t i);

    void clear();
    size_t size();

    page_item<PAGE_SHAPE>& back();
    void push_back(page_item<PAGE_SHAPE>& shape);

    itr_type begin() { return shapes.begin(); }
    itr_type end() { return shapes.end(); }
    
    itr_type erase(itr_type itr) { return shapes.erase(itr); }
    
  private:

    std::vector<page_item<PAGE_SHAPE> > shapes;
  };

  page_item<PAGE_SHAPES>::page_item()
  {}

  page_item<PAGE_SHAPES>::~page_item()
  {}

  nlohmann::json page_item<PAGE_SHAPES>::get()
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

  bool page_item<PAGE_SHAPES>::init_from(nlohmann::json& data)
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
	ss << "can not initialise page_item<PAGE_SHAPES> from "
	   << data.dump(2);

	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());	
      }
    
    return result;
  }

  void page_item<PAGE_SHAPES>::rotate(int angle, std::pair<double, double> delta)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& shape:shapes)
      {
	shape.rotate(angle, delta);
      }
  }
  
  page_item<PAGE_SHAPE>& page_item<PAGE_SHAPES>::operator[](size_t i)
  {    
    return shapes.at(i);
  }

  void page_item<PAGE_SHAPES>::clear()
  {
    shapes.clear();
  }

  size_t page_item<PAGE_SHAPES>::size()
  {
    return shapes.size();
  }

  page_item<PAGE_SHAPE>& page_item<PAGE_SHAPES>::back()
  {
    if(shapes.size()==0)
      {
	std::string message = "can not retrieve a shape, no shapes are known";
	LOG_S(ERROR) << message;
	throw std::logic_error(message);
      }
    
    return shapes.back();
  }

  void page_item<PAGE_SHAPES>::push_back(page_item<PAGE_SHAPE>& shape)
  {
    shapes.push_back(shape);
  }

}

#endif
