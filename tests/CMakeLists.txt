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

# --------------------------------------------------------
# Optional integration-test plugin
# --------------------------------------------------------
#
# This builds an actual plugin MODULE using the stub backend. It allows an
# integration test to verify that the host can discover and load the plugin.
#
# It is deliberately a different target from the production visa_plugin.
option(
  VISA_PLUGIN_BUILD_TEST_MODULE
  "Build a stub-backed plugin module for integration testing"
  OFF
)

if(VISA_PLUGIN_BUILD_TEST_MODULE)
  add_instrument_plugin(
    visa_plugin_test_module
    SOURCES
      "${PROJECT_SOURCE_DIR}/plugin.c"
    LINK_LIBRARIES
      visa_stub
      cjson
      instrument-log::instrument-log
      instrument-data::instrument-data
    INCLUDE_DIRS
      "${PROJECT_SOURCE_DIR}/stub"
  )

  set_target_properties(visa_plugin_test_module
    PROPERTIES
      FOLDER "Tests"
  )

  # Do not install this target. It is strictly a test artifact.
endif()
