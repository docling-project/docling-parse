//-*-C++-*-

#ifndef PYBIND_DOCLING_RESOURCES_H
#define PYBIND_DOCLING_RESOURCES_H

#include <Python.h>

#include <resources.h>

namespace docling
{
  
  class docling_resources
  {
  public:

    docling_resources();

    std::string get_resources_path();

  private:

    bool set_resources_path();
  };

  docling_resources::docling_resources()
  {
    set_resources_path();
  }

  std::string docling_resources::get_resources_path()
  {
    return resource_utils::get_resources_dir(true).string();
  }

  bool docling_resources::set_resources_path()
  {
    // Get the module object of your package
    PyObject* myPackageModule = PyImport_ImportModule("docling_parse");
    
    // Get the filename object of the module
    PyObject* filenameObj = PyModule_GetFilenameObject(myPackageModule);
    
    // Extract the string value of the filename
    const char* filename = PyUnicode_AsUTF8(filenameObj);
	
    std::filesystem::path __init__path(filename);

    std::filesystem::path package_path = __init__path.parent_path();
    std::filesystem::path resources_path = package_path / resource_utils::resources_relative_path;

    return resource_utils::set_resources_dir(resources_path);
  }  
  
}

#endif
