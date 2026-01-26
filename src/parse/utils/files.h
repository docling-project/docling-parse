//-*-C++-*-

#ifndef PDF_UTILS_FILES_H
#define PDF_UTILS_FILES_H

//#include <fstream>
//#include <algorithm>
//#include <dirent.h>
//#include <sys/stat.h>

namespace utils
{
  namespace filesystem
  {
    /*
      bool is_dir(std::string file_name)
      {
      struct stat st;
      lstat(file_name.c_str(), &st);

      if (S_ISDIR(st.st_mode))
      return true;

      return false;
      }
    */
    bool is_dir(const std::string& file_name) {
      return std::filesystem::is_directory(file_name);
    }

    /*
      bool is_file(std::string file_name)
      {
      struct stat st;
      lstat(file_name.c_str(), &st);

      if (S_ISDIR(st.st_mode))
      return false;

      return true;
      }
    */
    bool is_file(const std::string& file_name) {
      return std::filesystem::is_regular_file(file_name);
    }

    /*
      std::vector<std::string> list_files(std::string directory, bool basename=false)
      {
      std::vector<std::string> files = {};

      DIR* dp;
      struct dirent* dirp;

      if ((dp = opendir(directory.c_str())) == NULL)
      {
      LOG_S(ERROR) << "directory '" << directory << "' does not exist";
      }
      else
      {
      while ((dirp = readdir(dp)) != NULL)
      {
      std::string name = dirp->d_name;

      if(name != "." and
      name != ".." and
      is_file(directory + "/" + name))
      {
      if(basename)
      {
      files.push_back(name);
      }
      else
      {
      files.push_back(directory+"/"+name);
      }
      }
      }
      }

      return files;
      }
    */
    std::vector<std::string> list_files(const std::string& directory, bool basename = false)
    {
      std::vector<std::string> files;

      try
	{
	  for (const auto& entry : std::filesystem::directory_iterator(directory))
	    {
	      if (std::filesystem::is_regular_file(entry.path()))
		{
		  if (basename)
		    {
		      files.push_back(entry.path().filename().string());  // Get the file name only
		    }
		  else
		    {
		      files.push_back(entry.path().string());  // Full path
		    }
		}
	    }
	}
      catch (const std::filesystem::filesystem_error& e)
	{
	  LOG_S(ERROR) << "Error accessing directory " << directory.c_str() << ": " << e.what();
	}

      return files;
    }

  }
  
}

#endif
