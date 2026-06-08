#include <stddef.h>
#include <string.h>
#include <cmocka.h>
#include <instrument-plugin.h>
#include <stdarg.h>
void visa_stub_set_response(const char *resp);
// ============================================================
// Helper
// ============================================================

static PluginCommand make_cmd(const char *verb, bool expects_response) {
  PluginCommand cmd = {0};

  strncpy(cmd.id, "test-cmd", PLUGIN_MAX_STRING_LEN);
  strncpy(cmd.instrument_name, "test-instr", PLUGIN_MAX_STRING_LEN);
  strncpy(cmd.verb, verb, PLUGIN_MAX_STRING_LEN);

  cmd.param_count = 0;
  cmd.expects_response = expects_response;
  cmd.timeout_ms = 1000;

  return cmd;
}

// ============================================================
// Setup
// ============================================================

static int setup(void **state) {
  PluginConfig config = {0};

  strncpy(config.instrument_name, "test-instr", PLUGIN_MAX_STRING_LEN);
  strcpy(config.connection_json, "{\"address\":\"GPIB0::1::INSTR\"}");
  strcpy(config.api_definition_json, "{}");

  assert_int_equal(plugin_initialize(&config), 0);

  return 0;
}

static int teardown(void **state) {
  plugin_shutdown();
  return 0;
}

// ============================================================
// Tests
// ============================================================

static void test_metadata(void **state) {
  (void)state;
  PluginMetadata meta = plugin_get_metadata();

  assert_int_equal(meta.api_version, INSTRUMENT_PLUGIN_API_VERSION);

  assert_string_equal(meta.name, "NI-VISA Plugin");
  assert_string_equal(meta.protocol_type, "VISA");

  assert_true(strlen(meta.version) > 0);

  assert_true(strlen(meta.description) > 0);
}

static void test_no_response(void **state) {
  PluginCommand cmd = make_cmd("*CLS", false);
  PluginResponse resp;

  int rc = plugin_execute_command(&cmd, &resp);

  assert_int_equal(rc, 0);
  assert_true(resp.success);
  assert_false(resp.has_large_data);
}

static void test_integer_response(void **state) {
  visa_stub_set_response("42\n");

  PluginCommand cmd = make_cmd("MEAS?", true);
  PluginResponse resp;

  plugin_execute_command(&cmd, &resp);

  assert_true(resp.success);
  assert_int_equal(resp.return_value.type, PARAM_TYPE_INT64);
  assert_int_equal(resp.return_value.value.i64_val, 42);
}

static void test_double_response(void **state) {
  visa_stub_set_response("3.14\n");

  PluginCommand cmd = make_cmd("MEAS?", true);
  PluginResponse resp;

  plugin_execute_command(&cmd, &resp);

  assert_true(resp.success);
  assert_int_equal(resp.return_value.type, PARAM_TYPE_DOUBLE);
  assert_float_equal(resp.return_value.value.d_val, 3.14, 0.0001);
}

static void test_bool_response(void **state) {
  visa_stub_set_response("1\n");

  PluginCommand cmd = make_cmd("STAT?", true);
  PluginResponse resp;

  plugin_execute_command(&cmd, &resp);

  assert_true(resp.success);
  assert_int_equal(resp.return_value.type, PARAM_TYPE_BOOL);
  assert_true(resp.return_value.value.b_val);
}

static void test_string_response(void **state) {
  visa_stub_set_response("HELLO\n");

  PluginCommand cmd = make_cmd("ID?", true);
  PluginResponse resp;

  plugin_execute_command(&cmd, &resp);

  assert_true(resp.success);
  assert_int_equal(resp.return_value.type, PARAM_TYPE_STRING);
  assert_string_equal(resp.return_value.value.str_val, "HELLO");
}

static void test_array_response(void **state) {
  visa_stub_set_response("1.0,2.0,3.0\n");

  PluginCommand cmd = make_cmd("TRACE?", true);
  PluginResponse resp;

  plugin_execute_command(&cmd, &resp);

  assert_true(resp.success);

  assert_true(resp.has_large_data);
  assert_int_equal(resp.data_type, INST_DATA_FLOAT32);
  assert_int_equal(resp.data_element_count, 3);
}

// ============================================================
// Main
// ============================================================

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(test_no_response, setup, teardown),
      cmocka_unit_test(test_metadata),
      cmocka_unit_test_setup_teardown(test_integer_response, setup, teardown),
      cmocka_unit_test_setup_teardown(test_double_response, setup, teardown),
      cmocka_unit_test_setup_teardown(test_bool_response, setup, teardown),
      cmocka_unit_test_setup_teardown(test_string_response, setup, teardown),
      cmocka_unit_test_setup_teardown(test_array_response, setup, teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
