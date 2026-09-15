#include <instrument-log/inst_logging.h>
#include <plugin-host.h>
#include <scpi_simulator.h>
#include <instrument-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmocka.h>
#include <string.h>
#include <unistd.h>
#define TEST_PORT 5025
#define EPSILON 1e-6
typedef struct {
  ScpiSimulator sim;

  PluginConfig config;
} VisaTestContext;
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
static void wait_for_expectations(ScpiSimulator *sim, uint32_t timeout_ms) {
  uint32_t elapsed = 0;

  while (elapsed < timeout_ms) {

    if (scpi_simulator_all_expectations_met(sim)) {
      return;
    }

    usleep(1000);
    elapsed++;
  }

  fail_msg("Timed out waiting for simulator expectations");
}
static int setup(void **state) {
  VisaTestContext *ctx = calloc(1, sizeof(*ctx));
  assert_non_null(ctx);

  scpi_simulator_start(&ctx->sim, TEST_PORT, "\n");
  memset(&ctx->config, 0, sizeof(ctx->config));
  snprintf(ctx->config.address, sizeof(ctx->config.address),
           "TCPIP0::127.0.0.1::%hu::SOCKET", TEST_PORT);
  snprintf(ctx->config.instrument_name, sizeof(ctx->config.instrument_name),
           "MockVisa");
  snprintf(ctx->config.custom, sizeof(ctx->config.custom),
           "{\"term\":\"\\\\n\"}");
  *state = ctx;

  return 0;
}
static int teardown(void **state) {
  VisaTestContext *ctx = *state;

  plugin_shutdown();
  scpi_simulator_stop(&ctx->sim);

  free(ctx);
  return 0;
}
static void test_initialize(void **state) {
  VisaTestContext *ctx = *state;

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  scpi_simulator_wait_for_connection(&ctx->sim);
  assert_true(scpi_simulator_client_connected(&ctx->sim));
}
static void test_init_commands(void **state) {
  VisaTestContext *ctx = *state;
  snprintf(ctx->config.init_commands[0], PLUGIN_MAX_STRING_LEN, "*RST");
  snprintf(ctx->config.init_commands[1], PLUGIN_MAX_STRING_LEN, "SYST:REM");

  scpi_simulator_expect(&ctx->sim, "*RST", "");
  scpi_simulator_expect(&ctx->sim, "SYST:REM", "");

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  wait_for_expectations(&ctx->sim, 1000);
  assert_int_equal(scpi_simulator_hits(&ctx->sim, "*RST"), 1);
  assert_int_equal(scpi_simulator_hits(&ctx->sim, "SYST:REM"), 1);
}
static void test_query(void **state) {
  VisaTestContext *ctx = *state;

  scpi_simulator_expect(&ctx->sim, "*IDN?", "MOCK,QDAC,1234,1.0\n");

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  PluginCommand cmd = {0};
  snprintf(cmd.command, sizeof(cmd.command), "*IDN?");
  cmd.is_query = true;
  cmd.timeout_ms = 1000;
  PluginResponse *resp = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd, resp), 0);
  wait_for_expectations(&ctx->sim, 1000);

  assert_int_equal(plugin_response_count(resp), 4);
  {
    const Variable *v = plugin_response_get(resp, 0);
    assert_non_null(v);
    assert_int_equal(v->type, PARAM_TYPE_STRING);
    assert_string_equal(v->value.str_val, "MOCK");
  }
  {
    const Variable *v = plugin_response_get(resp, 1);
    assert_non_null(v);
    assert_int_equal(v->type, PARAM_TYPE_STRING);
    assert_string_equal(v->value.str_val, "QDAC");
  }
  {
    const Variable *v = plugin_response_get(resp, 2);
    assert_non_null(v);
    assert_int_equal(v->type, PARAM_TYPE_INT64);
    assert_int_equal(v->value.i64_val, 1234);
  }
  {
    const Variable *v = plugin_response_get(resp, 3);
    assert_non_null(v);
    assert_int_equal(v->type, PARAM_TYPE_DOUBLE);
    assert_double_equal(v->value.d_val, 1.0, EPSILON);
  }
  plugin_response_free(resp);
  assert_true(scpi_simulator_all_expectations_met(&ctx->sim));
}
int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(test_initialize, setup, teardown),
      cmocka_unit_test_setup_teardown(test_init_commands, setup, teardown),
      cmocka_unit_test_setup_teardown(test_query, setup, teardown),
  };

  return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
