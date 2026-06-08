find_package(cmocka CONFIG REQUIRED)

add_executable(test_visa_plugin
  test_visa_plugin.c
)
target_include_directories(test_visa_plugin PRIVATE
  ${VISA_INCLUDE_DIR}
)

target_link_libraries(test_visa_plugin
  visa_plugin_core
  cmocka::cmocka
)

add_test(NAME visa_plugin_tests COMMAND test_visa_plugin)
