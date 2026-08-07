
message(STATUS "entering in 'os_opts.cmake'")

if(WIN32)
   message(STATUS "compiling on windows")

   # set(LIB_LINK qpdf jpeg utf8 z)
   find_package(ZLIB)

   set(LIB_LINK)
   if(ZLIB_FOUND)
       set(OS_DEPENDENCIES ZLIB::ZLIB)
   else()
       set(OS_DEPENDENCIES)
   endif()

elseif(APPLE)
   message(STATUS "compiling on mac-osx")

   #set(CMAKE_MACOSX_RPATH 1)

   find_library(FoundationLib Foundation)
   find_package(ZLIB)
   #message("LIB: ${FoundationLib}")

   find_library(SystemConfigurationLib SystemConfiguration)
   #message("LIB: ${SystemConfigurationLib}")

   # set(LIB_LINK qpdf jpeg utf8 z)	
   set(OS_DEPENDENCIES ZLIB::ZLIB)


   set(LIB_LINK ${FoundationLib} ${SystemConfigurationLib})
   
elseif(UNIX)
   message(STATUS "compiling on linux")

   # set(LIB_LINK qpdf jpeg utf8 z)
   find_package(ZLIB)
   set(LIB_LINK dl m pthread rt resolv)
   set(OS_DEPENDENCIES ZLIB::ZLIB)

endif()
