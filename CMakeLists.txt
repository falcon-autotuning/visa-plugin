cmake_minimum_required(VERSION 3.20)

project(
  VISAPlugin
  VERSION 1.0.0
  LANGUAGES C
)

find_package(instrument-plugin-api CONFIG REQUIRED)
find_package(instrument-log CONFIG REQUIRED)
find_package(instrument-data CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)

set(VISA_PATH_HINTS)

# Environment variables (user-installed VISA)
if(DEFINED ENV{VISA_ROOT})
  list(APPEND VISA_PATH_HINTS $ENV{VISA_ROOT})
endif()

if(DEFINED ENV{NI_VISA_ROOT})
  list(APPEND VISA_PATH_HINTS $ENV{NI_VISA_ROOT})
endif()

# Default install locations
if(WIN32)
  list(APPEND VISA_PATH_HINTS
    "C:/Program Files/IVI Foundation/VISA/Win64"
    "C:/Program Files/IVI Foundation/VISA/WinNT"
    "C:/Program Files (x86)/IVI Foundation/VISA/WinNT"
  )
else()
  list(APPEND VISA_PATH_HINTS
    /usr
    /usr/local
    /opt/ni-visa
    /opt/visa
  )
endif()

find_path(VISA_INCLUDE_DIR
  NAMES visa.h
  HINTS ${VISA_PATH_HINTS}
  PATH_SUFFIXES include
)

find_library(VISA_LIBRARY
  NAMES visa visa64 nivisa visa32
  HINTS ${VISA_PATH_HINTS}
  PATH_SUFFIXES lib lib64 lib/x64 lib/msc
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(VISA
  REQUIRED_VARS VISA_LIBRARY VISA_INCLUDE_DIR
)

if(NOT VISA_FOUND)

  if(WIN32)
    message(FATAL_ERROR "NI-VISA not found on Windows. Please install NI-VISA.")
  endif()

  message(WARNING "NI-VISA not found. Using VISA stub implementation.")

  add_library(visa_stub STATIC stub/visa_stub.c)

  set(VISA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stub)

  target_include_directories(visa_stub PUBLIC
    ${VISA_INCLUDE_DIR}
  )

  add_library(VISA::VISA ALIAS visa_stub)


else()

  add_library(VISA::VISA UNKNOWN IMPORTED)

  set_target_properties(VISA::VISA PROPERTIES
    IMPORTED_LOCATION "${VISA_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${VISA_INCLUDE_DIR}"
  )

  message(STATUS "Found VISA include: ${VISA_INCLUDE_DIR}")
  message(STATUS "Found VISA library: ${VISA_LIBRARY}")

endif()

set(VISA_LINK_LIBRARIES
  VISA::VISA
  cjson
  instrument-plugin-api::instrument-plugin-api
  instrument-log::instrument-log
  instrument-data::instrument-data
)

add_instrument_plugin(visa_plugin
  SOURCES
    plugin.c
  LINK_LIBRARIES
    ${VISA_LINK_LIBRARIES}
  INCLUDE_DIRS
    ${VISA_INCLUDE_DIR}
)

add_library(visa_plugin_core STATIC
  plugin.c
)
target_include_directories(visa_plugin_core PRIVATE
  ${VISA_INCLUDE_DIR}
)

target_link_libraries(visa_plugin_core
  PRIVATE
    ${VISA_LINK_LIBRARIES}
)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS visa_plugin
  LIBRARY DESTINATION lib/instrument-plugins
  RUNTIME DESTINATION bin/instrument-plugins
)
