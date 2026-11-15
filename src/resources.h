//-*-C++-*-
// SPDX-License-Identifier: MIT

#ifndef RESOURCE_UTILS_H_
#define RESOURCE_UTILS_H_

#define LOGURU_WITH_STREAMS 1
#include <loguru.hpp>
#include <parse/utils/utf8_path.h>

class resource_utils
{
public:

  const static inline std::filesystem::path package_name = "docling_parse";
  const static inline std::filesystem::path resources_relative_path = "pdf_resources";

public:

  static bool set_resources_dir(std::filesystem::path path);

  static std::filesystem::path get_resources_dir(bool verify=true);

private:

  // ROOT_PATH is a UTF-8 source literal supplied by the build.
  static inline std::filesystem::path ROOT_DIR = utils::filesystem::from_utf8(ROOT_PATH);
  static inline std::filesystem::path PACKAGE_DIR = ROOT_DIR / package_name;
  static inline std::filesystem::path RESOURCES_DIR = PACKAGE_DIR / resources_relative_path;
};

bool resource_utils::set_resources_dir(std::filesystem::path path)
{
  RESOURCES_DIR = path;

  //std::cout << __FILE__ << ":" << __LINE__ << "\t"
  //<< "updated the resources-dir" << "\n";

  if(std::filesystem::exists(RESOURCES_DIR))
    {
      PACKAGE_DIR = RESOURCES_DIR.parent_path();
      ROOT_DIR = PACKAGE_DIR.parent_path();

      //LOG_S(INFO) << "updated resources-dir: " << RESOURCES_DIR;
      return true;
    }
  else
    {
      //LOG_S(ERROR) << "updated resources-dir to non-existant path: "
      //<< path << " at " << __FILE__ << ":" << __LINE__;
      return false;
    }
}

std::filesystem::path resource_utils::get_resources_dir(bool verify)
{
  if(verify and (not std::filesystem::exists(RESOURCES_DIR)))
    {
      // std::cout << __FILE__ << ":" << __LINE__ << "\t"
      LOG_S(ERROR) << __FILE__ << ":" << __LINE__ << "\t"
		   << "resources-dir does not exist ..." << "\n";
    }

  return RESOURCES_DIR;
}

#endif
