//-*-C++-*-

#ifndef QPDF_INSTRUCTION_H
#define QPDF_INSTRUCTION_H

namespace pdflib
{
  class qpdf_instruction
  {
  public:

    qpdf_instruction();
    qpdf_instruction(QPDFObjectHandle& obj_);

    ~qpdf_instruction();

    std::string unparse();

    bool is_integer();
    bool is_number();
    bool is_string();

    bool is_null();
    bool is_array();
    bool is_dict();
    
    
    int    to_int();
    double to_double();

    std::string to_byte_string();
    std::string to_char_string();
    std::string to_utf8_string();

  public:

    QPDFObjectHandle obj;

    std::string key="";
    std::string val="";
  };

  qpdf_instruction::qpdf_instruction()
  {}

  qpdf_instruction::qpdf_instruction(QPDFObjectHandle& obj_):
    obj(obj_),
    key(obj_.getTypeName()),
    val(obj_.unparse())
  {}

  qpdf_instruction::~qpdf_instruction()
  {}

  std::string qpdf_instruction::unparse()
  {
    return obj.unparse();
  }

  bool qpdf_instruction::is_integer()
  {
    return obj.isInteger();
  }

  bool qpdf_instruction::is_number()
  {
    return obj.isNumber();
  }

  bool qpdf_instruction::is_string()
  {
    return (obj.isName() or obj.isString());
  }

  bool qpdf_instruction::is_null()
  {
    return obj.isNull();
  }

  bool qpdf_instruction::is_array()
  {
    return obj.isArray();
  }

  bool qpdf_instruction::is_dict()
  {
    return obj.isDictionary();
  }
  
  int qpdf_instruction::to_int()  
  { 
    if(not obj.isInteger())
      {
	std::string message = "obj.isInteger() is false: " + obj.unparse();
	LOG_S(ERROR) << message;
	throw std::logic_error(message);
      }
    
    return obj.getIntValue(); 
  } 

  double qpdf_instruction::to_double() 
  {
    if(not obj.isNumber())
      {
	std::string message = "obj.isNumber() is false" + obj.unparse();
	LOG_S(ERROR) << message;
	throw std::logic_error(message);
      }
    
    return obj.getNumericValue(); 
  } 

  std::string qpdf_instruction::to_char_string() 
  {
    if (obj.isName() or obj.isString())
      {
        return obj.getStringValue(); 
      }
    else
      {
	std::stringstream ss;
        ss << "can not decode a string value for key: " << key 
	   << " and value: " << val;

	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    return "null";    
  }

  std::string qpdf_instruction::to_utf8_string() 
  {
    if (obj.isName())
      {
        return obj.getName(); 
      }
    else if (obj.isString())
      {
        return obj.getUTF8Value(); 
      }
    else
      {
	std::stringstream ss;
        ss << "can not decode a string value for key: " << key 
	   << " and value: " << val;

	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    return "null";
  } 

}

#endif
