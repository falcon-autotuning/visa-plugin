#include <cjson/cJSON.h>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// VISA
#include <visa.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif
#define MAX_READ_SIZE (1024 * 1024)
#define VISA_LOG_INFO(fmt, ...) LOG_INFO("Plugin", "VISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_DEBUG(fmt, ...) LOG_DEBUG("Plugin", "VISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_TRACE(fmt, ...) LOG_TRACE("Plugin", "VISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_WARN(fmt, ...) LOG_WARN("Plugin", "VISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_ERROR(fmt, ...) LOG_ERROR("Plugin", "VISA", fmt, ##__VA_ARGS__)

typedef struct {
  ViSession default_rm;
  ViSession instrument;
  char resource_address[PLUGIN_MAX_STRING_LEN];
  uint32_t timeout_ms;
  char termination_char[4];
  bool initialized;
} VISAPluginState;

static VISAPluginState g_state = {0};

static const char *get_json_string(cJSON *json, const char *key,
                                   const char *def) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  return (item && cJSON_IsString(item)) ? item->valuestring : def;
}

static int get_json_int(cJSON *json, const char *key, int def) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  return (item && cJSON_IsNumber(item)) ? item->valueint : def;
}

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;

  snprintf(meta.name, PLUGIN_MAX_STRING_LEN, "%s", "NI-VISA Plugin");
  snprintf(meta.version, PLUGIN_MAX_STRING_LEN, "%s", "2.0.0");
  snprintf(meta.protocol_type, PLUGIN_MAX_STRING_LEN, "%s", "VISA");
  snprintf(meta.description, PLUGIN_MAX_STRING_LEN, "%s",
           "NI VISA driver with shared-memory buffer support");
  VISA_LOG_DEBUG("Got the metadata for the VISA plugin\n");
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  VISA_LOG_INFO("init %s\n", config->instrument_name);

  cJSON *conn = cJSON_Parse(config->connection_json);
  if (!conn) {
    VISA_LOG_ERROR("Could not parse the ISA on instrument init\n");
    return -1;
  }

  const char *addr = get_json_string(conn, "address", "");
  if (!addr[0]) {
    cJSON_Delete(conn);
    VISA_LOG_ERROR("Could not parse the address field in the ISA\n");
    return -1;
  }

  snprintf(g_state.resource_address, sizeof(g_state.resource_address), "%s",
           addr);
  g_state.timeout_ms = get_json_int(conn, "timeout", 5000);
  VISA_LOG_DEBUG(
      "The selected timeout for the instrument in milliseonds is %d\n",
      g_state.timeout_ms);

  const char *term = get_json_string(conn, "termination", "\\n");
  VISA_LOG_DEBUG("The selected termination for the instrument is %s\n", term);
  if (strcmp(term, "\\n") == 0)
    snprintf(g_state.termination_char, sizeof(g_state.termination_char), "%s",
             "\n");
  else if (strcmp(term, "\\r") == 0)
    snprintf(g_state.termination_char, sizeof(g_state.termination_char), "%s",
             "\r");
  else if (strcmp(term, "\\r\\n") == 0)
    snprintf(g_state.termination_char, sizeof(g_state.termination_char), "%s",
             "\r\n");
  else
    snprintf(g_state.termination_char, sizeof(g_state.termination_char), "%s",
             term);

  cJSON_Delete(conn);

  if (viOpenDefaultRM(&g_state.default_rm) < VI_SUCCESS) {
    VISA_LOG_ERROR("Unable to start default rm for VISA\n");
    return -1;
  }

  if (viOpen(g_state.default_rm, g_state.resource_address, VI_NO_LOCK,
             g_state.timeout_ms, &g_state.instrument) < VI_SUCCESS) {
    VISA_LOG_ERROR(
        "Unable to lock instrument and check the instrument state\n");
    return -1;
  }

  g_state.initialized = true;
  return 0;
}

static size_t parse_float_array(char *buf, float *out, size_t max) {
  size_t count = 0;
  char *p = buf;

  while (*p && count < max) {
    out[count++] = strtof(p, &p);

    while (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r')
      p++;
  }
  return count;
}

static int visa_read_buffer(char **out_buf, size_t *out_len) {
  char *buffer = (char *)malloc(MAX_READ_SIZE);
  if (!buffer) {
    VISA_LOG_ERROR("Failed to allocate read buffer (%d bytes)", MAX_READ_SIZE);
    return -1;
  }

  ViUInt32 read = 0;

  ViStatus st =
      viRead(g_state.instrument, (ViBuf)buffer, MAX_READ_SIZE - 1, &read);

  VISA_LOG_DEBUG("viRead -> status=0x%08X bytes=%u", st, read);

  if (st < VI_SUCCESS && st != VI_SUCCESS_MAX_CNT) {
    VISA_LOG_ERROR("VISA read failed: 0x%08X", st);
    free(buffer);
    return -1;
  }

  buffer[read] = '\0';

  while (read > 0 && (buffer[read - 1] == '\n' || buffer[read - 1] == '\r')) {
    buffer[--read] = '\0';
  }

  *out_buf = buffer;
  *out_len = read;

  return 0;
}

static int parse_and_fill_response(const PluginCommand *cmd,
                                   PluginResponse *resp, char *buffer,
                                   size_t read_len) {
  bool has_delim = strchr(buffer, ',') || strchr(buffer, ' ');
  bool has_digit = strpbrk(buffer, "0123456789");

  if (has_delim && has_digit) {
    VISA_LOG_DEBUG("Detected array");

    size_t est = 1;
    for (size_t i = 0; i < read_len; i++)
      if (buffer[i] == ',' || buffer[i] == ' ')
        est++;

    void *shm_ptr = NULL;

    const char *buf_id = data_manager_create_buffer_zero_copy(
        cmd->instrument_name, cmd->id, INST_DATA_FLOAT32, est, &shm_ptr);

    if (!buf_id || !shm_ptr) {
      VISA_LOG_ERROR("Buffer allocation failed (%zu)", est);
      snprintf(resp->error_message, sizeof(resp->error_message), "%s",
               "buffer alloc failed");
      resp->success = false;
      return -1;
    }

    float *out = (float *)shm_ptr;
    size_t count = parse_float_array(buffer, out, est);

    resp->has_large_data = true;
    snprintf(resp->data_buffer_id, sizeof(resp->data_buffer_id), "%s", buf_id);
    resp->data_element_count = count;
    resp->data_type = INST_DATA_FLOAT32;

    snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "buffer:%s count=%zu",
             buf_id, count);

    VISA_LOG_DEBUG("Parsed %zu elements -> %s", count, buf_id);

    resp->success = true;
    return 0;
  }

  if (strcmp(buffer, "1") == 0 || strcasecmp(buffer, "ON") == 0) {
    resp->return_value.type = PARAM_TYPE_BOOL;
    resp->return_value.value.b_val = true;
    goto done;
  }

  if (strcmp(buffer, "0") == 0 || strcasecmp(buffer, "OFF") == 0) {
    resp->return_value.type = PARAM_TYPE_BOOL;
    resp->return_value.value.b_val = false;
    goto done;
  }

  char *end;
  long long i = strtoll(buffer, &end, 10);

  if (end != buffer && *end == '\0') {
    resp->return_value.type = PARAM_TYPE_INT64;
    resp->return_value.value.i64_val = i;
    goto done;
  }

  char *end2;
  double d = strtod(buffer, &end2);

  if (end2 != buffer && *end2 == '\0') {
    resp->return_value.type = PARAM_TYPE_DOUBLE;
    resp->return_value.value.d_val = d;
    goto done;
  }

  resp->return_value.type = PARAM_TYPE_STRING;
  snprintf(resp->return_value.value.str_val,
           sizeof(resp->return_value.value.str_val), "%s", buffer);

done:

  snprintf(resp->text_response, sizeof(resp->text_response), "%s", buffer);
  resp->success = true;

  VISA_LOG_DEBUG("Scalar parsed: '%s'", buffer);

  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {

  memset(resp, 0, sizeof(PluginResponse));
  snprintf(resp->command_id, sizeof(resp->command_id), "%s", cmd->id);

  snprintf(resp->instrument_name, sizeof(resp->instrument_name), "%s",
           cmd->instrument_name);

  resp->return_value.type = PARAM_TYPE_NONE;

  if (!g_state.initialized) {
    resp->success = false;
    VISA_LOG_ERROR("Not initialized VISA plugin");
    snprintf(resp->error_message, sizeof(resp->error_message), "%s",
             "Not initialized");
    return -1;
  }

  viSetAttribute(g_state.instrument, VI_ATTR_TMO_VALUE, cmd->timeout_ms);

  char cmd_buf[PLUGIN_MAX_PAYLOAD];
  snprintf(cmd_buf, sizeof(cmd_buf), "%s%s", cmd->verb,
           g_state.termination_char);

  ViUInt32 written = 0;
  ViStatus write_status =
      viWrite(g_state.instrument, (ViBuf)cmd_buf, strlen(cmd_buf), &written);
  if (write_status < VI_SUCCESS) {
    resp->success = false;
    VISA_LOG_ERROR("Write failed: 0x%08X", write_status);
    snprintf(resp->error_message, sizeof(resp->error_message), "%s",
             "VISA write failed");
    return -1;
  }
  if (!cmd->expects_response) {
    resp->success = true;
    VISA_LOG_DEBUG("Write successful (no response expected)");
    return 0;
  }

  char *buffer = NULL;
  size_t read_len = 0;
  int rc = -1;

  if (visa_read_buffer(&buffer, &read_len) == 0) {
    rc = parse_and_fill_response(cmd, resp, buffer, read_len);
  } else {
    resp->success = false;
    snprintf(resp->error_message, sizeof(resp->error_message), "%s",
             "VISA read failed");
  }

  free(buffer);
  return rc;
}

// =========================
// Shutdown
// =========================

void plugin_shutdown(void) {
  if (g_state.instrument)
    viClose(g_state.instrument);

  if (g_state.default_rm)
    viClose(g_state.default_rm);

  g_state.initialized = false;
}
