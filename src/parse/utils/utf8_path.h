// SPDX-License-Identifier: MIT

#ifndef PDF_UTILS_UTF8_PATH_H
#define PDF_UTILS_UTF8_PATH_H

#include <filesystem>
#include <string>

namespace utils
{
  namespace filesystem
  {
    // Python and the parser's string paths use UTF-8. On Windows a native
    // filesystem path is wide, and narrow file opens use the ANSI codepage.
    inline std::filesystem::path from_utf8(const std::string& value)
    {
      return std::filesystem::path(std::u8string(value.begin(), value.end()));
    }

    inline std::string to_utf8(const std::filesystem::path& value)
    {
      const auto utf8 = value.u8string();
      return std::string(utf8.begin(), utf8.end());
    }
  }
}

#endif
