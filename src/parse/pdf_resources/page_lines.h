//-*-C++-*-

#ifndef PDF_PAGE_LINES_RESOURCE_H
#define PDF_PAGE_LINES_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_LINES>
  {
    typedef typename std::vector<pdf_resource<PAGE_LINE> >::iterator itr_type;
    
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    void rotate(int angle, std::pair<double, double> delta);
    
    pdf_resource<PAGE_LINE>& operator[](size_t i);

    void clear();
    size_t size();

    pdf_resource<PAGE_LINE>& back();
    void push_back(pdf_resource<PAGE_LINE>& line);

    itr_type begin() { return lines.begin(); }
    itr_type end() { return lines.end(); }
    
    itr_type erase(itr_type itr) { return lines.erase(itr); }
    
  private:

    std::vector<pdf_resource<PAGE_LINE> > lines;
  };

  pdf_resource<PAGE_LINES>::pdf_resource()
  {}

  pdf_resource<PAGE_LINES>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_LINES>::get()
  {
    nlohmann::json result = nlohmann::json::array();

    for(auto& line:lines)
      {
	if(line.size()>0)
	  {
	    result.push_back(line.get());
	  }
      }

    return result;
  }

  bool pdf_resource<PAGE_LINES>::init_from(nlohmann::json& data)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    bool result=true;
    
    if(data.is_array())
      {
	lines.clear();
	lines.resize(data.size());

	for(int i=0; i<data.size(); i++)	  
	  {
	    result = (result and lines.at(i).init_from(data[i]));
	  }
      }
    else
      {
	std::stringstream ss;
	ss << "can not initialise pdf_resource<PAGE_LINES> from "
	   << data.dump(2);

	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());	
      }
    
    return result;
  }

  void pdf_resource<PAGE_LINES>::rotate(int angle, std::pair<double, double> delta)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& line:lines)
      {
	line.rotate(angle, delta);
      }
  }
  
  pdf_resource<PAGE_LINE>& pdf_resource<PAGE_LINES>::operator[](size_t i)
  {    
    return lines.at(i);
  }

  void pdf_resource<PAGE_LINES>::clear()
  {
    lines.clear();
  }

  size_t pdf_resource<PAGE_LINES>::size()
  {
    return lines.size();
  }

  pdf_resource<PAGE_LINE>& pdf_resource<PAGE_LINES>::back()
  {
    if(lines.size()==0)
      {
	std::string message = "can not retrieve a line, no lines are known";
	LOG_S(ERROR) << message;
	throw std::logic_error(message);
      }
    
    return lines.back();
  }

  void pdf_resource<PAGE_LINES>::push_back(pdf_resource<PAGE_LINE>& line)
  {
    lines.push_back(line);
  }

}

#endif
