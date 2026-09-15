find_package(cmocka CONFIG REQUIRED)

# --------------------------------------------------------
# Test-only VISA stub
# --------------------------------------------------------
#
# This target is created only when BUILD_TESTING is enabled because this
# entire directory is added conditionally by the root CMakeLists.txt.
#
# It is not installed and cannot be linked into the production plugin.
add_library(visa_stub STATIC
  "${PROJECT_SOURCE_DIR}/stub/visa_stub.c"
)

target_include_directories(visa_stub
  PUBLIC
    "${PROJECT_SOURCE_DIR}/stub"
)

set_target_properties(visa_stub
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER "Tests/Stubs"
)

# --------------------------------------------------------
# Test-only plugin core
# --------------------------------------------------------
#
# Compile the real plugin implementation against the stub visa.h.
#
# Unit tests link this static library directly, allowing them to call the
# plugin implementation without dynamically loading a MODULE library.
add_library(visa_plugin_test_core STATIC
  "${PROJECT_SOURCE_DIR}/plugin.c"
)

target_include_directories(visa_plugin_test_core
  PRIVATE
    "${PROJECT_SOURCE_DIR}/stub"
)

target_compile_definitions(visa_plugin_test_core
  PRIVATE
    INSTRUMENT_PLUGIN_BUILD
)

target_link_libraries(visa_plugin_test_core
  PRIVATE
    cjson
    instrument-plugin-api::plugin
    instrument-log::instrument-log
    instrument-data::instrument-data
)

set_target_properties(visa_plugin_test_core
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER "Tests"
)

# --------------------------------------------------------
# Unit-test executable
# --------------------------------------------------------
add_executable(test_visa_plugin
  test_visa_plugin.c
)

target_include_directories(test_visa_plugin
  PRIVATE
    "${PROJECT_SOURCE_DIR}/stub"
)

target_link_libraries(test_visa_plugin
  PRIVATE
    visa_plugin_test_core
    visa_stub
    cmocka::cmocka
    instrument-plugin-api::host
)

set_target_properties(test_visa_plugin
  PROPERTIES
    FOLDER "Tests"
)

add_test(
  NAME test_visa_plugin
  COMMAND test_visa_plugin
)

## Integration tests

#
# Try to locate RsVisa
#
find_path(
  RSVISA_INCLUDE_DIR
  NAMES visa.h
  PATH_SUFFIXES
    rsvisa
    ni-visa
  REQUIRED
)

find_library(
  RSVISA_LIBRARY
  NAMES
    rsvisa
    visa
  REQUIRED
)

if(NOT RSVISA_INCLUDE_DIR)
  message(FATAL_ERROR "Could not locate visa.h for RsVisa")
endif()

if(NOT RSVISA_LIBRARY)
  message(FATAL_ERROR "Could not locate RsVisa library")
endif()

message(STATUS "RsVisa include: ${RSVISA_INCLUDE_DIR}")
message(STATUS "RsVisa library: ${RSVISA_LIBRARY}")

# --------------------------------------------------------
# SCPI simulator
# --------------------------------------------------------

add_library(scpi_simulator STATIC
    "${PROJECT_SOURCE_DIR}/sim/scpi_simulator.c"
)

target_include_directories(scpi_simulator
    PUBLIC
      "${PROJECT_SOURCE_DIR}/sim"
)

set_target_properties(scpi_simulator
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# --------------------------------------------------------
# Plugin compiled against REAL VISA
# --------------------------------------------------------

add_library(visa_plugin_integration_core STATIC
    ${PROJECT_SOURCE_DIR}/plugin.c
)

target_compile_definitions(visa_plugin_integration_core
    PRIVATE
        INSTRUMENT_PLUGIN_BUILD
)

target_include_directories(visa_plugin_integration_core
    PRIVATE
        ${RSVISA_INCLUDE_DIR}
)

target_link_libraries(visa_plugin_integration_core
    PRIVATE
        ${RSVISA_LIBRARY}
        cjson
        instrument-plugin-api::plugin
        instrument-log::instrument-log
        instrument-data::instrument-data
)

set_target_properties(visa_plugin_integration_core
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# --------------------------------------------------------
# Integration test executable
# --------------------------------------------------------

add_executable(integration_simulator
    integration_simulator.c
)

target_include_directories(integration_simulator
    PRIVATE
        ${RSVISA_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(integration_simulator
    PRIVATE
        visa_plugin_integration_core
        scpi_simulator

        cmocka::cmocka
        instrument-plugin-api::host

        ${RSVISA_LIBRARY}
)


add_test(
    NAME integration_simulator
    COMMAND integration_simulator
)
