#include <instrument-data.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>
#include <instrument-plugin.h>
#include <instrument-log/inst_logging.h>
#include <plugin-api.h>
#include <plugin-host.h>
#include <stdarg.h>

void visa_stub_set_response_delay(uint32_t delay_ms);
void visa_stub_set_response(const char *resp);
void visa_stub_set_chunk_size(size_t chunk_size);
void visa_stub_reset(void);

// ============================================================
// Helper
// ============================================================
// Currently we detect queries by the inclusion of a '?' in the command string.
// This is not a robust solution, but it is sufficient for testing purposes.
static PluginCommand make_cmd(const char *command) {
  PluginCommand cmd = {0};

  snprintf(cmd.id, PLUGIN_MAX_STRING_LEN, "%s", "test-cmd");
  snprintf(cmd.command, PLUGIN_MAX_STRING_LEN, "%s", command);

  cmd.params = param_storage_create();
  cmd.timeout_ms = 1000;
  cmd.is_query = (strchr(command, '?') != NULL);
  return cmd;
}

// ============================================================
// Setup
// ============================================================

static int common_setup(const char *test_name, void **state) {
  (void)state;
  LOG_INFO("TEST", "LOG", "===== START TEST: %s =====", test_name);

  visa_stub_reset();
  PluginConfig config = {0};

  snprintf(config.instrument_name, PLUGIN_MAX_STRING_LEN, "%s", "test-instr");
  snprintf(config.address, PLUGIN_MAX_STRING_LEN, "%s", "GPIB0::1::INSTR");
  assert_int_equal(plugin_initialize(&config), 0);

  return 0;
}
// Unique wrapper functions for setup
#define DEFINE_SETUP_WRAPPER(test_func)                                        \
  static int setup_##test_func(void **state) {                                 \
    return common_setup(#test_func, state);                                    \
  }

static int setup_custom_term(void **state) {
  LOG_INFO("TEST", "LOG", "===== START TEST: test_custom_term =====");
  visa_stub_reset();
  PluginConfig config = {0};

  snprintf(config.instrument_name, PLUGIN_MAX_STRING_LEN, "%s", "test-instr");
  snprintf(config.address, PLUGIN_MAX_STRING_LEN, "%s", "GPIB0::1::INSTR");
  snprintf(config.custom, PLUGIN_MAX_STRING_LEN, "%s", "{\"term\":\"\\r\\n\"}");
  assert_int_equal(plugin_initialize(&config), 0);

  return 0;
}
static int setup_crazy_instrument(void **state) {
  LOG_INFO("TEST", "LOG", "===== START TEST: test_crazy_instrument =====");
  visa_stub_reset();
  PluginConfig config = {0};

  snprintf(config.instrument_name, PLUGIN_MAX_STRING_LEN, "%s", "test-instr");
  snprintf(config.address, PLUGIN_MAX_STRING_LEN, "%s", "GPIB0::1::INSTR");
  snprintf(config.custom, PLUGIN_MAX_STRING_LEN, "%s",
           "{\"term\":\"\\r\\n\",\"arr_d\":\"$$\", "
           "\"arg_d\":\"^^\",\"tout\":2000}");
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
  PluginCommand cmd = make_cmd("CLS");

  visa_stub_set_response("\n");
  PluginResponse *resp = plugin_response_create();

  int rc = plugin_execute_command(&cmd, resp);

  assert_int_equal(rc, 0);
  assert_int_equal(plugin_response_count(resp), 0);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_no_response_alt_term(void **state) {
  PluginCommand cmd = make_cmd("CLS");

  visa_stub_set_response("\r\n");
  PluginResponse *resp = plugin_response_create();

  int rc = plugin_execute_command(&cmd, resp);

  assert_int_equal(rc, 0);
  assert_int_equal(plugin_response_count(resp), 0);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_integer_response(void **state) {
  visa_stub_set_response("42\n");

  PluginCommand cmd = make_cmd("MEAS?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_INT64);
  assert_int_equal(v->value.i64_val, 42);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_integer_response_extra_space(void **state) {
  visa_stub_set_response(" 42\n");

  PluginCommand cmd = make_cmd("MEAS?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_INT64);
  assert_int_equal(v->value.i64_val, 42);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_integer_response_extra_spaces(void **state) {
  visa_stub_set_response("  42\n");

  PluginCommand cmd = make_cmd("MEAS?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_INT64);
  assert_int_equal(v->value.i64_val, 42);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_double_response(void **state) {
  visa_stub_set_response("3.14\n");

  PluginCommand cmd = make_cmd("MEAS?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_DOUBLE);
  assert_float_equal(v->value.d_val, 3.14, 0.0001);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_double_response_extra_spaces(void **state) {
  visa_stub_set_response("  3.14 \n");

  PluginCommand cmd = make_cmd("MEAS?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_DOUBLE);
  assert_float_equal(v->value.d_val, 3.14, 0.0001);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_bool_response(void **state) {
  visa_stub_set_response("1\n");

  PluginCommand cmd = make_cmd("STAT?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_BOOL);
  assert_true(v->value.b_val);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_string_response(void **state) {
  visa_stub_set_response("HELLO\n");

  PluginCommand cmd = make_cmd("ID?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_STRING);
  assert_string_equal(v->value.str_val, "HELLO");

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_array_response(void **state) {
  visa_stub_set_response("1.0;2.0;3.0\n");

  PluginCommand cmd = make_cmd("TRACE?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);
  const Variable *v = plugin_response_get(resp, 0);
  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_BUFFER);
  assert_true(strlen(v->value.str_val) > 0);
  DataBuffer *buf = data_manager_get_buffer(v->value.str_val);
  void *data = data_buffer_data(buf);
  size_t data_count = data_buffer_element_count(buf);
  for (size_t i = 0; i < data_count; ++i) {
    double val = ((double *)data)[i];
    assert_float_equal(val, (double)(i + 1), 0.0001);
  }
  data_manager_release_buffer(v->value.str_val);
  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_mixed_response(void **state) {
  visa_stub_set_response("404,\"No error\",4.2,ON\n");

  PluginCommand cmd = make_cmd("SYSTEM:ERROR?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 4);

  const Variable *v0 = plugin_response_get(resp, 0);
  assert_non_null(v0);
  assert_int_equal(v0->type, PARAM_TYPE_INT64);
  assert_int_equal(v0->value.i64_val, 404);

  const Variable *v1 = plugin_response_get(resp, 1);
  assert_non_null(v1);
  assert_int_equal(v1->type, PARAM_TYPE_STRING);
  assert_string_equal(v1->value.str_val, "No error");

  const Variable *v2 = plugin_response_get(resp, 2);
  assert_non_null(v2);
  assert_int_equal(v2->type, PARAM_TYPE_DOUBLE);
  assert_float_equal(v2->value.d_val, 4.2, 0.0001);

  const Variable *v3 = plugin_response_get(resp, 3);
  assert_non_null(v3);
  assert_int_equal(v3->type, PARAM_TYPE_BOOL);
  assert_true(v3->value.b_val);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_mixed_response_with_spaces(void **state) {
  visa_stub_set_response("404, \"No error\", 4.2, ON\n");

  PluginCommand cmd = make_cmd("SYSTEM:ERROR?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 4);

  const Variable *v0 = plugin_response_get(resp, 0);
  assert_non_null(v0);
  assert_int_equal(v0->type, PARAM_TYPE_INT64);
  assert_int_equal(v0->value.i64_val, 404);

  const Variable *v1 = plugin_response_get(resp, 1);
  assert_non_null(v1);
  assert_int_equal(v1->type, PARAM_TYPE_STRING);
  assert_string_equal(v1->value.str_val, "No error");

  const Variable *v2 = plugin_response_get(resp, 2);
  assert_non_null(v2);
  assert_int_equal(v2->type, PARAM_TYPE_DOUBLE);
  assert_float_equal(v2->value.d_val, 4.2, 0.0001);

  const Variable *v3 = plugin_response_get(resp, 3);
  assert_non_null(v3);
  assert_int_equal(v3->type, PARAM_TYPE_BOOL);
  assert_true(v3->value.b_val);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_mixed_response_with_even_more_spaces(void **state) {
  visa_stub_set_response("404 , \"No error\" , 4.2 , ON\n");

  PluginCommand cmd = make_cmd("SYSTEM:ERROR?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 4);

  const Variable *v0 = plugin_response_get(resp, 0);
  assert_non_null(v0);
  assert_int_equal(v0->type, PARAM_TYPE_INT64);
  assert_int_equal(v0->value.i64_val, 404);

  const Variable *v1 = plugin_response_get(resp, 1);
  assert_non_null(v1);
  assert_int_equal(v1->type, PARAM_TYPE_STRING);
  assert_string_equal(v1->value.str_val, "No error");

  const Variable *v2 = plugin_response_get(resp, 2);
  assert_non_null(v2);
  assert_int_equal(v2->type, PARAM_TYPE_DOUBLE);
  assert_float_equal(v2->value.d_val, 4.2, 0.0001);

  const Variable *v3 = plugin_response_get(resp, 3);
  assert_non_null(v3);
  assert_int_equal(v3->type, PARAM_TYPE_BOOL);
  assert_true(v3->value.b_val);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_array_and_normal(void **state) {
  visa_stub_set_response("404,1;2;3;4,4.2,1.1;2.2;3.3;4.4\n");

  PluginCommand cmd = make_cmd("SYSTEM:ERROR?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 4);

  const Variable *v0 = plugin_response_get(resp, 0);
  assert_non_null(v0);
  assert_int_equal(v0->type, PARAM_TYPE_INT64);
  assert_int_equal(v0->value.i64_val, 404);

  const Variable *v1 = plugin_response_get(resp, 1);
  assert_non_null(v1);
  assert_int_equal(v1->type, PARAM_TYPE_BUFFER);
  assert_true(strlen(v1->value.str_val) > 0);
  {
    DataBuffer *buf = data_manager_get_buffer(v1->value.str_val);
    void *data = data_buffer_data(buf);
    size_t data_count = data_buffer_element_count(buf);
    for (size_t i = 0; i < data_count; ++i) {
      int64_t val = ((int64_t *)data)[i];
      assert_int_equal(val, (int64_t)(i + 1));
    }
    data_manager_release_buffer(v1->value.str_val);
  }

  const Variable *v2 = plugin_response_get(resp, 2);
  assert_non_null(v2);
  assert_int_equal(v2->type, PARAM_TYPE_DOUBLE);
  assert_float_equal(v2->value.d_val, 4.2, 0.0001);

  const Variable *v3 = plugin_response_get(resp, 3);
  assert_non_null(v3);
  assert_int_equal(v3->type, PARAM_TYPE_BUFFER);
  assert_true(strlen(v3->value.str_val) > 0);
  {
    DataBuffer *buf = data_manager_get_buffer(v3->value.str_val);
    void *data = data_buffer_data(buf);
    size_t data_count = data_buffer_element_count(buf);
    for (size_t i = 0; i < data_count; ++i) {
      double val = ((double *)data)[i];
      assert_float_equal(val, (double)(i + 1 + ((double)(i + 1) / 10.0)),
                         0.0001);
    }
    data_manager_release_buffer(v3->value.str_val);
  }

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_crazy_instrument(void **state) {
  visa_stub_set_response("404^^1$$2$$3$$4^^4.2^^1.1$$2.2$$3.3$$4.4\r\n");

  PluginCommand cmd = make_cmd("SYSTEM:ERROR?");
  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 4);

  const Variable *v0 = plugin_response_get(resp, 0);
  assert_non_null(v0);
  assert_int_equal(v0->type, PARAM_TYPE_INT64);
  assert_int_equal(v0->value.i64_val, 404);

  const Variable *v1 = plugin_response_get(resp, 1);
  assert_non_null(v1);
  assert_int_equal(v1->type, PARAM_TYPE_BUFFER);
  assert_true(strlen(v1->value.str_val) > 0);
  {
    DataBuffer *buf = data_manager_get_buffer(v1->value.str_val);
    void *data = data_buffer_data(buf);
    size_t data_count = data_buffer_element_count(buf);
    for (size_t i = 0; i < data_count; ++i) {
      int64_t val = ((int64_t *)data)[i];
      assert_int_equal(val, (int64_t)(i + 1));
    }
    data_manager_release_buffer(v1->value.str_val);
  }

  const Variable *v2 = plugin_response_get(resp, 2);
  assert_non_null(v2);
  assert_int_equal(v2->type, PARAM_TYPE_DOUBLE);
  assert_float_equal(v2->value.d_val, 4.2, 0.0001);

  const Variable *v3 = plugin_response_get(resp, 3);
  assert_non_null(v3);
  assert_int_equal(v3->type, PARAM_TYPE_BUFFER);
  assert_true(strlen(v3->value.str_val) > 0);
  {
    DataBuffer *buf = data_manager_get_buffer(v3->value.str_val);
    void *data = data_buffer_data(buf);
    size_t data_count = data_buffer_element_count(buf);
    for (size_t i = 0; i < data_count; ++i) {
      double val = ((double *)data)[i];
      assert_float_equal(val, (double)(i + 1 + ((double)(i + 1) / 10.0)),
                         0.0001);
    }
    data_manager_release_buffer(v3->value.str_val);
  }

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_delayed_response_before_timeout(void **state) {
  (void)state;

  visa_stub_reset();

  visa_stub_set_response("42\n");
  visa_stub_set_response_delay(250);

  PluginCommand cmd = make_cmd("MEAS?");
  cmd.timeout_ms = 1000;

  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);

  const Variable *v = plugin_response_get(resp, 0);

  assert_non_null(v);
  assert_int_equal(v->type, PARAM_TYPE_INT64);

  assert_int_equal(v->value.i64_val, 42);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_response_timeout(void **state) {
  (void)state;

  visa_stub_reset();

  visa_stub_set_response("42\n");
  visa_stub_set_response_delay(1500);

  PluginCommand cmd = make_cmd("MEAS?");
  cmd.timeout_ms = 1000;

  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 1);

  assert_int_equal(plugin_response_count(resp), 0);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

static void test_fragmented_termination(void **state) {
  (void)state;

  visa_stub_reset();

  visa_stub_set_response("12345\n");

  /*
   * Reads:
   * "123"
   * "45\n"
   */
  visa_stub_set_chunk_size(3);

  PluginCommand cmd = make_cmd("MEAS?");

  PluginResponse *resp = plugin_response_create();

  assert_int_equal(plugin_execute_command(&cmd, resp), 0);

  assert_int_equal(plugin_response_count(resp), 1);

  const Variable *v = plugin_response_get(resp, 0);

  assert_non_null(v);

  assert_int_equal(v->type, PARAM_TYPE_INT64);

  assert_int_equal(v->value.i64_val, 12345);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

// ============================================================
// Group Setup / Teardown
// ============================================================

static int group_setup(void **state) {
  (void)state;
  inst_log_init("visa_test.log", INST_LOG_TRACE, "VISA_TestHarness", 1048576,
                3);
  return 0;
}

static int group_teardown(void **state) {
  (void)state;
  inst_log_flush();
  inst_log_shutdown();
  return 0;
}

// ============================================================
// Main
// ============================================================
DEFINE_SETUP_WRAPPER(test_no_response)
DEFINE_SETUP_WRAPPER(test_integer_response)
DEFINE_SETUP_WRAPPER(test_integer_response_extra_space)
DEFINE_SETUP_WRAPPER(test_integer_response_extra_spaces)
DEFINE_SETUP_WRAPPER(test_double_response)
DEFINE_SETUP_WRAPPER(test_double_response_extra_spaces)
DEFINE_SETUP_WRAPPER(test_bool_response)
DEFINE_SETUP_WRAPPER(test_string_response)
DEFINE_SETUP_WRAPPER(test_array_response)
DEFINE_SETUP_WRAPPER(test_mixed_response)
DEFINE_SETUP_WRAPPER(test_mixed_response_with_spaces)
DEFINE_SETUP_WRAPPER(test_mixed_response_with_even_more_spaces)
DEFINE_SETUP_WRAPPER(test_delayed_response_before_timeout)
DEFINE_SETUP_WRAPPER(test_response_timeout)
DEFINE_SETUP_WRAPPER(test_fragmented_termination)
DEFINE_SETUP_WRAPPER(test_array_and_normal)
int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(test_no_response, setup_test_no_response,
                                      teardown),
      cmocka_unit_test(test_metadata),
      cmocka_unit_test_setup_teardown(test_integer_response,
                                      setup_test_integer_response, teardown),
      cmocka_unit_test_setup_teardown(test_integer_response_extra_space,
                                      setup_test_integer_response_extra_space,
                                      teardown),
      cmocka_unit_test_setup_teardown(test_integer_response_extra_spaces,
                                      setup_test_integer_response_extra_spaces,
                                      teardown),
      cmocka_unit_test_setup_teardown(test_double_response,
                                      setup_test_double_response, teardown),
      cmocka_unit_test_setup_teardown(test_double_response_extra_spaces,
                                      setup_test_double_response_extra_spaces,
                                      teardown),
      cmocka_unit_test_setup_teardown(test_bool_response,
                                      setup_test_bool_response, teardown),
      cmocka_unit_test_setup_teardown(test_string_response,
                                      setup_test_string_response, teardown),
      cmocka_unit_test_setup_teardown(test_array_response,
                                      setup_test_array_response, teardown),
      cmocka_unit_test_setup_teardown(test_mixed_response,
                                      setup_test_mixed_response, teardown),
      cmocka_unit_test_setup_teardown(test_mixed_response_with_spaces,
                                      setup_test_mixed_response_with_spaces,
                                      teardown),
      cmocka_unit_test_setup_teardown(
          test_mixed_response_with_even_more_spaces,
          setup_test_mixed_response_with_even_more_spaces, teardown),
      cmocka_unit_test_setup_teardown(
          test_delayed_response_before_timeout,
          setup_test_delayed_response_before_timeout, teardown),
      cmocka_unit_test_setup_teardown(test_response_timeout,
                                      setup_test_response_timeout, teardown),
      cmocka_unit_test_setup_teardown(test_fragmented_termination,
                                      setup_test_fragmented_termination,
                                      teardown),
      cmocka_unit_test_setup_teardown(test_array_and_normal,
                                      setup_test_array_and_normal, teardown),
      cmocka_unit_test_setup_teardown(test_no_response_alt_term,
                                      setup_custom_term, teardown),
      cmocka_unit_test_setup_teardown(test_crazy_instrument,
                                      setup_crazy_instrument, teardown)};

  return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
