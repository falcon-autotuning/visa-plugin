cmake_minimum_required(VERSION 3.20)

project(
  VISAPlugin
  VERSION 1.1.8
  LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

option(BUILD_TESTS "Enable building tests" OFF)
option(USE_CCACHE "Enable ccache or sccache if available" ON)
option(
  VISA_PLUGIN_BUILD_PLUGIN
  "Build the production VISA plugin using real VISA"
  ON
)

# --------------------------------------------------------
# Dependencies
# --------------------------------------------------------
find_package(instrument-plugin-api CONFIG REQUIRED)
find_package(instrument-log CONFIG REQUIRED)
find_package(instrument-data CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)

# --------------------------------------------------------
# Production plugin
# --------------------------------------------------------
if(VISA_PLUGIN_BUILD_PLUGIN)
  message(STATUS "Production VISA plugin enabled")
  message(STATUS "Searching for a real VISA installation")

  # --------------------------------------------------------
  # Expected VISA installation locations
  # --------------------------------------------------------
  if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      # 64-bit plugin.
      set(_VISA_LIBRARY_NAMES
      visa64
      nivisa64
      visa
    )

      set(_VISA_LIBRARY_PATHS
      "C:/Program Files/IVI Foundation/VISA/Win64/Lib_x64/msc"
    )

      set(_VISA_INCLUDE_PATHS
      "C:/Program Files/IVI Foundation/VISA/Win64/include"
    )
    else()
      # 32-bit plugin.
      set(_VISA_LIBRARY_NAMES
      visa32
      nivisa32
      visa
    )

      set(_VISA_LIBRARY_PATHS
      "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/lib/msc"
    )

      set(_VISA_INCLUDE_PATHS
      "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/include"
    )
    endif()

    find_library(
    VISA_LIBRARY
    NAMES
      ${_VISA_LIBRARY_NAMES}
    HINTS
      ${_VISA_LIBRARY_PATHS}
    NO_DEFAULT_PATH
    DOC
      "Path to the real VISA import library"
  )

    find_path(
    VISA_INCLUDE_DIR
    NAMES
      visa.h
    HINTS
      ${_VISA_INCLUDE_PATHS}
    NO_DEFAULT_PATH
    DOC
      "Directory containing the real VISA header"
  )
  else()
    # Linux and other Unix-like systems.
    set(_VISA_LIBRARY_NAMES
    visa
    nivisa
    rsvisa
    rsvisa64
  )

    set(_VISA_LIBRARY_PATHS
    "/usr/lib"
    "/usr/lib64"
    "/usr/local/lib"
    "/usr/local/lib64"
    "/usr/lib/x86_64-linux-gnu"
  )

    set(_VISA_INCLUDE_PATHS
    "/usr/include"
    "/usr/include/rsvisa"
    "/usr/include/ni-visa"
    "/usr/local/include"
    "/usr/local/include/rsvisa"
    "/usr/local/include/ni-visa"
    )

    find_library(
    VISA_LIBRARY
    NAMES
      ${_VISA_LIBRARY_NAMES}
    HINTS
      ${_VISA_LIBRARY_PATHS}
    PATH_SUFFIXES
      lib
      lib64
      Runtime/lib
    DOC
      "Path to the real VISA shared library"
  )

    find_path(
    VISA_INCLUDE_DIR
    NAMES
      visa.h
      Visa/visa.h
    HINTS
      ${_VISA_INCLUDE_PATHS}
    PATH_SUFFIXES
      include
      Include
      visa
      Visa
    DOC
      "Directory containing the real VISA header"
  )
  endif()

  # --------------------------------------------------------
  # Validate VISA detection
  # --------------------------------------------------------
  include(FindPackageHandleStandardArgs)

  find_package_handle_standard_args(
  VISA
  REQUIRED_VARS
    VISA_LIBRARY
    VISA_INCLUDE_DIR
)

  string(
  REPLACE ";"
  "\n  "
  _VISA_LIBRARY_NAMES_DISPLAY
  "${_VISA_LIBRARY_NAMES}"
)

  string(
  REPLACE ";"
  "\n  "
  _VISA_LIBRARY_PATHS_DISPLAY
  "${_VISA_LIBRARY_PATHS}"
)

  string(
  REPLACE ";"
  "\n  "
  _VISA_INCLUDE_PATHS_DISPLAY
  "${_VISA_INCLUDE_PATHS}"
)

  if(NOT VISA_FOUND)
    message(FATAL_ERROR
    "The production VISA plugin was requested, but compatible VISA "
    "development files could not be found.\n"
    "\n"
    "Searched library names:\n"
    "  ${_VISA_LIBRARY_NAMES_DISPLAY}\n"
    "\n"
    "Expected library locations:\n"
    "  ${_VISA_LIBRARY_PATHS_DISPLAY}\n"
    "\n"
    "Expected header locations:\n"
    "  ${_VISA_INCLUDE_PATHS_DISPLAY}\n"
    "\n"
    "VISA_LIBRARY='${VISA_LIBRARY}'\n"
    "VISA_INCLUDE_DIR='${VISA_INCLUDE_DIR}'\n"
    "\n"
    "Specify the files explicitly if they were installed elsewhere:\n"
    "  -DVISA_LIBRARY=/path/to/libvisa.so\n"
    "  -DVISA_INCLUDE_DIR=/path/to/include"
  )
  endif()

  # ------------------------------------------------------
  # Validate VISA detection
  # ------------------------------------------------------
  include(FindPackageHandleStandardArgs)

  find_package_handle_standard_args(
    VISA
    REQUIRED_VARS
      VISA_LIBRARY
      VISA_INCLUDE_DIR
  )

  if(NOT VISA_FOUND)
    message(FATAL_ERROR
      "The production VISA plugin was requested, but the real VISA "
      "development files could not be found.\n"
      "\n"
      "Expected library locations:\n"
      "  ${_VISA_LIBRARY_PATHS}\n"
      "\n"
      "Expected header locations:\n"
      "  ${_VISA_INCLUDE_PATHS}\n"
      "\n"
      "VISA_LIBRARY='${VISA_LIBRARY}'\n"
      "VISA_INCLUDE_DIR='${VISA_INCLUDE_DIR}'"
    )
  endif()

  message(STATUS "Using real VISA")
  message(STATUS "VISA library: ${VISA_LIBRARY}")
  message(STATUS "VISA include: ${VISA_INCLUDE_DIR}")

  # ------------------------------------------------------
  # Real VISA imported target
  # ------------------------------------------------------
  if(NOT TARGET VISA::VISA)
    add_library(VISA::VISA UNKNOWN IMPORTED)

    set_target_properties(
      VISA::VISA
      PROPERTIES
        IMPORTED_LOCATION
          "${VISA_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES
          "${VISA_INCLUDE_DIR}"
    )
  endif()

  # ------------------------------------------------------
  # Production plugin
  # ------------------------------------------------------
  #
  # This target always links against the real VISA import library.
  # There is no stub fallback.
  add_instrument_plugin(
    visa_plugin
    SOURCES
      plugin.c
    LINK_LIBRARIES
      VISA::VISA
      cjson
      instrument-log::instrument-log
      instrument-data::instrument-data
    INCLUDE_DIRS
      "${VISA_INCLUDE_DIR}"
  )

  # ------------------------------------------------------
  # Installation
  # ------------------------------------------------------
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS visa_plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/instrument-plugins
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/instrument-plugins
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/visa-plugin-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
  )

  configure_package_config_file(
    cmake/visa-plugin-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/visa-plugin-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/visa-plugin
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/visa-plugin-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/visa-plugin-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/visa-plugin
  )
else()
  message(STATUS "Production VISA plugin disabled")
  message(STATUS "Real VISA will not be searched for")
endif()

# --------------------------------------------------------
# Unit tests
# --------------------------------------------------------
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
