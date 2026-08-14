cmake_minimum_required(VERSION 3.20)

project(
  VISAPlugin
  VERSION 1.1.1
  LANGUAGES C
)

# --------------------------------------------------------
# Dependencies
# --------------------------------------------------------
find_package(instrument-plugin-api CONFIG REQUIRED)

if(TARGET instrument-plugin-api::instrument-plugin-api-plugin AND NOT TARGET instrument-plugin-api::plugin)
  add_library(instrument-plugin-api::plugin INTERFACE IMPORTED)
  set_target_properties(instrument-plugin-api::plugin PROPERTIES
    INTERFACE_LINK_LIBRARIES instrument-plugin-api::instrument-plugin-api-plugin
  )
endif()

if(TARGET instrument-plugin-api::instrument-plugin-api-host AND NOT TARGET instrument-plugin-api::host)
  add_library(instrument-plugin-api::host INTERFACE IMPORTED)
  set_target_properties(instrument-plugin-api::host PROPERTIES
    INTERFACE_LINK_LIBRARIES instrument-plugin-api::instrument-plugin-api-host
  )
endif()

find_package(instrument-log CONFIG REQUIRED)
find_package(instrument-data CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)

# --------------------------------------------------------
# Always build VISA stub (used for tests + fallback)
# --------------------------------------------------------
add_library(visa_stub STATIC stub/visa_stub.c)

target_include_directories(visa_stub PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/stub
)

# --------------------------------------------------------
# Find real VISA (optional on non-Windows)
# --------------------------------------------------------
unset(VISA_LIBRARY CACHE)
unset(VISA_INCLUDE_DIR CACHE)

if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(VISA_NAMES visa64 nivisa64)
    set(VISA_PATHS
      "C:/Program Files/IVI Foundation/VISA/Win64/Lib_x64/msc"
    )
  else()
    set(VISA_NAMES visa32)
    set(VISA_PATHS
      "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/lib/msc"
    )
  endif()

  find_library(VISA_LIBRARY
    NAMES ${VISA_NAMES}
    PATHS ${VISA_PATHS}
    NO_DEFAULT_PATH
  )

  set(VISA_INCLUDE_DIR
    "C:/Program Files/IVI Foundation/VISA/Win64/include"
  )

else()
  find_library(VISA_LIBRARY NAMES visa nivisa)
  find_path(VISA_INCLUDE_DIR NAMES visa.h)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(VISA
  REQUIRED_VARS VISA_LIBRARY VISA_INCLUDE_DIR
)

# --------------------------------------------------------
# Decide REAL vs STUB
# --------------------------------------------------------
if(VISA_FOUND)
  message(STATUS "Using real VISA")

  add_library(VISA::VISA UNKNOWN IMPORTED)
  set_target_properties(VISA::VISA PROPERTIES
    IMPORTED_LOCATION "${VISA_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${VISA_INCLUDE_DIR}"
  )

  set(VISA_DEP VISA::VISA)
  set(VISA_INCLUDE_USED ${VISA_INCLUDE_DIR})

else()
  message(WARNING "VISA not found — falling back to stub")

  set(VISA_DEP visa_stub)
  set(VISA_INCLUDE_USED ${CMAKE_CURRENT_SOURCE_DIR}/stub)
endif()

message(STATUS "VISA include: ${VISA_INCLUDE_USED}")
if(VISA_FOUND)
  message(STATUS "VISA library: ${VISA_LIBRARY}")
endif()

# --------------------------------------------------------
# Core library (NO VISA dependency)
# --------------------------------------------------------
add_library(visa_plugin_core STATIC
  plugin.c
)

target_include_directories(visa_plugin_core PRIVATE
  ${VISA_INCLUDE_USED}
)

target_link_libraries(visa_plugin_core
  PRIVATE
    cjson
    instrument-plugin-api::plugin
    instrument-log::instrument-log
    instrument-data::instrument-data
)

# --------------------------------------------------------
# Plugin (uses real VISA if available)
# --------------------------------------------------------
add_instrument_plugin(visa_plugin
  SOURCES
    plugin.c
  LINK_LIBRARIES
    ${VISA_DEP}
    cjson
    instrument-plugin-api::plugin
    instrument-log::instrument-log
    instrument-data::instrument-data
  INCLUDE_DIRS
    ${VISA_INCLUDE_USED}
)

# --------------------------------------------------------
# Tests (always stub)
# --------------------------------------------------------
enable_testing()

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

# --------------------------------------------------------
# Install
# --------------------------------------------------------
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
