
message(STATUS "entering in extlib_qpdf.cmake")

set(ext_name "qpdf")

if(USE_SYSTEM_DEPS)
    find_package(PkgConfig)
    pkg_check_modules(libqpdf REQUIRED IMPORTED_TARGET libqpdf)

    add_library(${ext_name} ALIAS PkgConfig::libqpdf) 
    #set_target_properties(${ext_name} PROPERTIES INTERFACE_LINK_LIBRARIES "${libqpdf_LIBRARIES}")
    #set_target_properties(${ext_name} PROPERTIES INTERFACE_LINK_DIRECTORIES "${libqpdf_LIBRARY_DIRS}")
    #set_target_properties(${ext_name} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${libqpdf_INCLUDEDIR}")

else()

  include(ExternalProject)
  include(CMakeParseArguments)

  set(QPDF_URL https://github.com/qpdf/qpdf.git)
  
  set(QPDF_TAG v12.2.0 )
  set(QPDF_LIB ${EXTERNALS_PREFIX_PATH}/lib/libqpdf.a)
  set(JPEG_LIB ${EXTERNALS_PREFIX_PATH}/lib/libjpeg.a)

  if(UNIX)
      set(QPDF_EXTRA_CXX_FLAGS "-fPIC ")
      set(QPDF_EXTRA_C_FLAGS "-fPIC ")
      set(BUILD_SHARED_LIBS ON)
  elseif(WIN32)
      set(QPDF_EXTRA_CXX_FLAGS "")
      set(QPDF_EXTRA_C_FLAGS "")
      set(BUILD_SHARED_LIBS OFF)
  else()
      set(QPDF_EXTRA_CXX_FLAGS " ")
      set(QPDF_EXTRA_C_FLAGS " ")
      set(BUILD_SHARED_LIBS ON)
  endif()

  ExternalProject_Add(extlib_qpdf

      PREFIX extlib_qpdf

      DEPENDS extlib_jpeg

      UPDATE_COMMAND ""

      GIT_REPOSITORY ${QPDF_URL}
      GIT_TAG        ${QPDF_TAG}
      
      BUILD_ALWAYS OFF

      INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

      CMAKE_ARGS \\ 
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\ 
      -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS} \\
      -DCMAKE_BUILD_TYPE=Release \\
      -DUSE_IMPLICIT_CRYPTO=OFF \\ 
      -DREQUIRE_CRYPTO_NATIVE=ON \\ 
      -DCMAKE_CXX_FLAGS=${QPDF_EXTRA_CXX_FLAGS} \\ 
      -DCMAKE_C_FLAGS=${QPDF_EXTRA_C_FLAGS} \\ 
      -DLIBJPEG_LIBRARY=${JPEG_LIB} \\ 
      -DLIBJPEG_LIBRARIES=${JPEG_LIB} \\ 
      -DLIBJPEG_LIB_PATH=${JPEG_LIB} \\ 
      -DLIBJPEG_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib \\ 
      -DLIBJPEG_H_PATH=${EXTERNALS_PREFIX_PATH}/include \\ 
      -DLIBJPEG_INCLUDEDIR=${EXTERNALS_PREFIX_PATH}/include \\ 
      -Dpc_libjpeg_LIBRARY=${JPEG_LIB} \\ 
      -Dpc_libjpeg_LIBRARIES=${JPEG_LIB} \\ 
      -Dpc_libjpeg_LIB_PATH=${JPEG_LIB} \\ 
      -Dpc_libjpeg_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib \\ 
      -Dpc_libjpeg_H_PATH=${EXTERNALS_PREFIX_PATH}/include \\ 
      -Dpc_libjpeg_INCLUDEDIR=${EXTERNALS_PREFIX_PATH}/include \\ 
      -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES} \\ 
      -DINSTALL_EXAMPLES=OFF \\ 
      -DBUILD_DOC_DIST=OFF \\ 
      -DCMAKE_INSTALL_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib \\ 
      -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH}
      
      LOG_DOWNLOAD ON
  )

  add_library(${ext_name} STATIC IMPORTED)
  add_dependencies(${ext_name} extlib_qpdf)
  set(EXT_INCLUDE_DIRS ${EXTERNALS_PREFIX_PATH}/include/qpdf)
  # This is a workaround for the fact that included directories of an imported
  # target should exist in the filesystem already at the configuration time.
  # ref: https://gitlab.kitware.com/cmake/cmake/-/issues/15052
  file(MAKE_DIRECTORY ${EXT_INCLUDE_DIRS})
  set_target_properties(${ext_name} PROPERTIES
    IMPORTED_LOCATION ${QPDF_LIB}
    INTERFACE_LINK_LIBRARIES ${JPEG_LIB}
    INTERFACE_LINK_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/lib
    INTERFACE_INCLUDE_DIRECTORIES ${EXT_INCLUDE_DIRS}
  )

endif()
