#include <signal.h>
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
static void dump_simulator_log(ScpiSimulator *sim) {
  FILE *f = fopen("simulator_log.txt", "w");

  if (f == NULL) {
    return;
  }

  fprintf(f, "%s", scpi_simulator_command_log(sim));

  fclose(f);
}
// ============================================================
// Group Setup / Teardown
// ============================================================

static int group_setup(void **state) {
  (void)state;
  signal(SIGPIPE, SIG_IGN);
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
static int setup_tcp(void **state) {
  VisaTestContext *ctx = calloc(1, sizeof(*ctx));
  assert_non_null(ctx);

  scpi_simulator_start_tcp(&ctx->sim, TEST_PORT, "\n");
  snprintf(ctx->config.address, sizeof(ctx->config.address),
           "TCPIP0::127.0.0.1::%hu::SOCKET", TEST_PORT);
  snprintf(ctx->config.instrument_name, sizeof(ctx->config.instrument_name),
           "MockVisa");
  snprintf(ctx->config.custom, sizeof(ctx->config.custom),
           "{\"term\":\"\\\\n\",\"acks\":\"ON\"}");
  *state = ctx;

  return 0;
}
static int setup_serial(void **state) {
  VisaTestContext *ctx = calloc(1, sizeof(*ctx));
  assert_non_null(ctx);

  scpi_simulator_start_serial(&ctx->sim, "/dev/tnt1", "\n");
  snprintf(ctx->config.address, sizeof(ctx->config.address), "ASRL10::INSTR");
  ctx->config.baud_rate = 9600;
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
  assert_true(scpi_simulator_wait_for_hits(&ctx->sim, "SYST:REM", 1, 1000));
  assert_int_equal(scpi_simulator_hits(&ctx->sim, "*RST"), 1);
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
  assert_true(scpi_simulator_wait_for_hits(&ctx->sim, "*IDN?", 1, 1000));

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
}

static void test_set(void **state) {
  VisaTestContext *ctx = *state;

  scpi_simulator_expect(&ctx->sim, "SET", "\n");

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  PluginCommand cmd = {0};
  snprintf(cmd.command, sizeof(cmd.command), "SET");
  cmd.is_query = false;
  cmd.timeout_ms = 1000;
  PluginResponse *resp = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd, resp), 0);
  assert_true(scpi_simulator_wait_for_hits(&ctx->sim, "SET", 1, 1000));

  assert_int_equal(plugin_response_count(resp), 0);
  plugin_response_free(resp);
}

static void test_double_set(void **state) {
  VisaTestContext *ctx = *state;

  scpi_simulator_expect_persistent(&ctx->sim, "SET", "\n");

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  PluginCommand cmd = {0};
  snprintf(cmd.command, sizeof(cmd.command), "SET");
  cmd.is_query = false;
  cmd.timeout_ms = 1000;
  PluginResponse *resp = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd, resp), 0);
  assert_int_equal(plugin_execute_command(&cmd, resp), 0);
  plugin_response_free(resp);
  assert_true(scpi_simulator_wait_for_hits(&ctx->sim, "SET", 2, 1000));
}

static void test_back_to_back_query(void **state) {
  VisaTestContext *ctx = *state;

  scpi_simulator_expect(&ctx->sim, "*IDN?", "MOCK,QDAC,1234,1.0\n");
  scpi_simulator_expect(&ctx->sim, "GET_VOLT", "5.2\n");

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  PluginCommand cmd1 = {0};
  snprintf(cmd1.command, sizeof(cmd1.command), "*IDN?");
  cmd1.is_query = true;
  cmd1.timeout_ms = 1000;
  PluginResponse *resp1 = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd1, resp1), 0);

  PluginCommand cmd2 = {0};
  snprintf(cmd2.command, sizeof(cmd2.command), "GET_VOLT");
  cmd2.is_query = true;
  cmd2.timeout_ms = 1000;
  PluginResponse *resp2 = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd2, resp2), 0);
  assert_true(scpi_simulator_wait_for_hits(&ctx->sim, "GET_VOLT", 1, 1000));
  {
    assert_int_equal(plugin_response_count(resp1), 4);
    {
      const Variable *v = plugin_response_get(resp1, 0);
      assert_non_null(v);
      assert_int_equal(v->type, PARAM_TYPE_STRING);
      assert_string_equal(v->value.str_val, "MOCK");
    }
    {
      const Variable *v = plugin_response_get(resp1, 1);
      assert_non_null(v);
      assert_int_equal(v->type, PARAM_TYPE_STRING);
      assert_string_equal(v->value.str_val, "QDAC");
    }
    {
      const Variable *v = plugin_response_get(resp1, 2);
      assert_non_null(v);
      assert_int_equal(v->type, PARAM_TYPE_INT64);
      assert_int_equal(v->value.i64_val, 1234);
    }
    {
      const Variable *v = plugin_response_get(resp1, 3);
      assert_non_null(v);
      assert_int_equal(v->type, PARAM_TYPE_DOUBLE);
      assert_double_equal(v->value.d_val, 1.0, EPSILON);
    }
  }
  {
    assert_int_equal(plugin_response_count(resp2), 1);
    const Variable *v = plugin_response_get(resp2, 0);
    assert_non_null(v);
    assert_int_equal(v->type, PARAM_TYPE_DOUBLE);
    assert_double_equal(v->value.d_val, 5.2, EPSILON);
  }
  plugin_response_free(resp1);
  plugin_response_free(resp2);
  scpi_simulator_all_expectations_met(&ctx->sim);
}

static void test_double_set_then_query(void **state) {
  // This test is harder to pass on ni-visa than rsvisa
  VisaTestContext *ctx = *state;

  scpi_simulator_expect_persistent(&ctx->sim, "SET", "\n");
  scpi_simulator_expect(&ctx->sim, "GET_VOLT", "5.255555\n");

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  PluginCommand cmd1 = {0};
  snprintf(cmd1.command, sizeof(cmd1.command), "SET");
  cmd1.is_query = false;
  cmd1.timeout_ms = 1000;
  PluginResponse *resp1 = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd1, resp1), 0);
  assert_int_equal(plugin_execute_command(&cmd1, resp1), 0);

  PluginCommand cmd2 = {0};
  snprintf(cmd2.command, sizeof(cmd2.command), "GET_VOLT");
  cmd2.is_query = true;
  cmd2.timeout_ms = 1000;
  PluginResponse *resp2 = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd2, resp2), 0);
  {
    assert_int_equal(plugin_response_count(resp2), 1);
    const Variable *v = plugin_response_get(resp2, 0);
    assert_non_null(v);
    assert_int_equal(v->type, PARAM_TYPE_DOUBLE);
    assert_double_equal(v->value.d_val, 5.255555, EPSILON);
  }
  plugin_response_free(resp1);
  plugin_response_free(resp2);
  assert_true(scpi_simulator_wait_for_hits(&ctx->sim, "GET_VOLT", 1, 1000));
  assert_int_equal(scpi_simulator_hits(&ctx->sim, "SET"), 2);
  dump_simulator_log(&ctx->sim);
}
static void test_double_set_then_query_delay(void **state) {
  // This test is harder to pass on ni-visa than rsvisa
  VisaTestContext *ctx = *state;

  scpi_simulator_expect_persistent_delayed(&ctx->sim, "SET", "\n", 1000);
  scpi_simulator_expect_delayed(&ctx->sim, "GET_VOLT", "5.255555\n", 100000);

  assert_int_equal(plugin_initialize(&ctx->config), 0);
  PluginCommand cmd1 = {0};
  snprintf(cmd1.command, sizeof(cmd1.command), "SET");
  cmd1.is_query = false;
  cmd1.timeout_ms = 1000;
  PluginResponse *resp1 = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd1, resp1), 0);
  assert_int_equal(plugin_execute_command(&cmd1, resp1), 0);

  PluginCommand cmd2 = {0};
  snprintf(cmd2.command, sizeof(cmd2.command), "GET_VOLT");
  cmd2.is_query = true;
  cmd2.timeout_ms = 1000;
  PluginResponse *resp2 = plugin_response_create();
  assert_int_equal(plugin_execute_command(&cmd2, resp2), 0);
  {
    assert_int_equal(plugin_response_count(resp2), 1);
    const Variable *v = plugin_response_get(resp2, 0);
    assert_non_null(v);
    assert_int_equal(v->type, PARAM_TYPE_DOUBLE);
    assert_double_equal(v->value.d_val, 5.255555, EPSILON);
  }
  plugin_response_free(resp1);
  plugin_response_free(resp2);
  assert_true(scpi_simulator_wait_for_hits(&ctx->sim, "GET_VOLT", 1, 1000));
  assert_int_equal(scpi_simulator_hits(&ctx->sim, "SET"), 2);
  dump_simulator_log(&ctx->sim);
}
typedef struct {
  CMUnitTestFunction test;
  const char *name;
} TestCase;
static const TestCase TEST_CASES[] = {
    {test_initialize, "test_initialize"},
    {test_init_commands, "test_init_commands"},
    {test_query, "test_query"},
    {test_back_to_back_query, "test_back_to_back_query"},
    {test_set, "test-set"},
    {test_double_set, "test_double_set"},
    {test_double_set_then_query, "test_double_set_then_query"},
    {test_double_set_then_query_delay, "test_double_set_then_query_delay"},
};
int main(void) {
  const size_t num_tests = sizeof(TEST_CASES) / sizeof(TEST_CASES[0]);

  struct CMUnitTest tcp_tests[num_tests];

  for (size_t i = 0; i < num_tests; ++i) {
    tcp_tests[i] = (struct CMUnitTest){
        .name = TEST_CASES[i].name,
        .test_func = TEST_CASES[i].test,
        .setup_func = setup_tcp,
        .teardown_func = teardown,
    };
  }

  printf("\n=== TCP TESTS ===\n");

  int ret = _cmocka_run_group_tests(TEST_CASES[0].name, tcp_tests, num_tests,
                                    group_setup, group_teardown);

  if (ret != 0) {
    return ret;
  }
  return ret;
  // DEPRECATED
  //   struct CMUnitTest serial_tests[num_tests];
  //
  //   for (size_t i = 0; i < num_tests; ++i) {
  //     serial_tests[i] = (struct CMUnitTest){
  //         .name = TEST_CASES[i].name,
  //         .test_func = TEST_CASES[i].test,
  //         .setup_func = setup_serial,
  //         .teardown_func = teardown,
  //     };
  //   }
  //
  //   printf("\n=== SERIAL TESTS ===\n");
  //
  //   return _cmocka_run_group_tests(TEST_CASES[0].name, serial_tests,
  //   num_tests,
  //                                  group_setup, group_teardown);
}
