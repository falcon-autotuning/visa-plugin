find_package(cmocka CONFIG REQUIRED)

add_executable(test_visa_plugin
  test_visa_plugin.c
)

target_include_directories(test_visa_plugin PRIVATE
  ${CMAKE_SOURCE_DIR}/stub
)

target_link_libraries(test_visa_plugin
  PRIVATE
    visa_plugin_core
    visa_stub
    cmocka::cmocka
)

add_test(NAME test_visa_plugin COMMAND test_visa_plugin)
