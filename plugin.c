#include <cjson/cJSON.h>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <plugin-api.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// VISA
#include <visa.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <time.h>
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
  char instrument_name[PLUGIN_MAX_STRING_LEN];
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
static uint64_t get_time_ms(void) {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
#endif
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

uint8_t plugin_initialize(const PluginConfig *config) {
  VISA_LOG_INFO("init %s\n", config->instrument_name);

  snprintf(g_state.instrument_name, sizeof(g_state.instrument_name), "%s",
           config->instrument_name);

  if (!config->address[0]) {
    VISA_LOG_ERROR("Could not parse the address field in the configuration\n");
    return 1;
  }

  snprintf(g_state.resource_address, sizeof(g_state.resource_address), "%s",
           config->address);

  g_state.timeout_ms = 5000;
  const char *term = "\\n";

  cJSON *custom_json = cJSON_Parse(config->custom);
  if (custom_json) {
    g_state.timeout_ms = get_json_int(custom_json, "timeout", 5000);
    term = get_json_string(custom_json, "termination", "\\n");
  }

  VISA_LOG_DEBUG(
      "The selected timeout for the instrument in milliseconds is %d\n",
      g_state.timeout_ms);
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

  if (custom_json) {
    cJSON_Delete(custom_json);
  }

  if (viOpenDefaultRM(&g_state.default_rm) < VI_SUCCESS) {
    VISA_LOG_ERROR("Unable to start default rm for VISA\n");
    return 1;
  }

  if (viOpen(g_state.default_rm, g_state.resource_address, VI_NO_LOCK,
             g_state.timeout_ms, &g_state.instrument) < VI_SUCCESS) {
    VISA_LOG_ERROR(
        "Unable to lock instrument and check the instrument state\n");
    return 1;
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

static int visa_read_buffer(char **out_buf, size_t *out_len,
                            uint32_t timeout_ms) {
  char *buffer = malloc(MAX_READ_SIZE);
  if (!buffer) {
    VISA_LOG_ERROR("Failed to allocate read buffer");
    return -1;
  }

  uint64_t start_time = get_time_ms();

  size_t total_read = 0;
  size_t term_len = strlen(g_state.termination_char);

  while (total_read < (MAX_READ_SIZE - 1)) {
    uint64_t elapsed = get_time_ms() - start_time;

    if (elapsed >= timeout_ms) {
      VISA_LOG_ERROR("Overall VISA timeout reached (%u ms)", timeout_ms);
      free(buffer);
      return -1;
    }

    uint32_t remaining = timeout_ms - (uint32_t)elapsed;

    /*
     * Poll in small increments but always allow
     * the final read to consume the exact time
     * remaining.
     */
    uint32_t read_timeout = (remaining > 100) ? 100 : remaining;

    viSetAttribute(g_state.instrument, VI_ATTR_TMO_VALUE, read_timeout);

    ViUInt32 chunk_read = 0;

    ViStatus st =
        viRead(g_state.instrument, (ViBuf)(buffer + total_read),
               (ViUInt32)(MAX_READ_SIZE - total_read - 1), &chunk_read);

    VISA_LOG_TRACE("viRead -> status=0x%08X bytes=%u remaining=%u", st,
                   chunk_read, remaining);

    if (chunk_read > 0) {
      total_read += chunk_read;
      buffer[total_read] = '\0';

      if (term_len > 0 && total_read >= term_len &&
          memcmp(buffer + total_read - term_len, g_state.termination_char,
                 term_len) == 0) {
        total_read -= term_len;
        buffer[total_read] = '\0';

        *out_buf = buffer;
        *out_len = total_read;
        return 0;
      }
    }

    /*
     * Poll timeout.
     * Just continue until the overall timeout expires.
     */
    if (st == VI_ERROR_TMO) {
      continue;
    }

    if (st < VI_SUCCESS && st != VI_SUCCESS_MAX_CNT) {
      VISA_LOG_ERROR("VISA read failed: 0x%08X", st);
      free(buffer);
      return -1;
    }
  }

  VISA_LOG_ERROR("Read buffer exceeded maximum size");

  free(buffer);
}

static void parse_single_token(const char *token, Variable *var) {
  // Trim leading/trailing whitespace
  while (*token == ' ' || *token == '\t' || *token == '\r' || *token == '\n') {
    token++;
  }
  size_t len = strlen(token);
  while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t' ||
                     token[len - 1] == '\r' || token[len - 1] == '\n')) {
    len--;
  }

  // Strip quotes if any
  const char *start = token;
  if (len >= 2 && ((start[0] == '"' && start[len - 1] == '"') ||
                   (start[0] == '\'' && start[len - 1] == '\''))) {
    start++;
    len -= 2;
  }

  // Temporary buffer for null-terminated token
  char *temp = malloc(len + 1);
  memcpy(temp, start, len);
  temp[len] = '\0';

  snprintf(var->name, sizeof(var->name), "value");

  if (strcmp(temp, "1") == 0 || strcasecmp(temp, "ON") == 0) {
    var->type = PARAM_TYPE_BOOL;
    var->value.b_val = true;
    free(temp);
    return;
  }

  if (strcmp(temp, "0") == 0 || strcasecmp(temp, "OFF") == 0) {
    var->type = PARAM_TYPE_BOOL;
    var->value.b_val = false;
    free(temp);
    return;
  }

  char *end;
  long long i = strtoll(temp, &end, 10);
  if (end != temp && *end == '\0') {
    var->type = PARAM_TYPE_INT64;
    var->value.i64_val = i;
    free(temp);
    return;
  }

  char *end2;
  double d = strtod(temp, &end2);
  if (end2 != temp && *end2 == '\0') {
    var->type = PARAM_TYPE_DOUBLE;
    var->value.d_val = d;
    free(temp);
    return;
  }

  var->type = PARAM_TYPE_STRING;
  snprintf(var->value.str_val, sizeof(var->value.str_val), "%s", temp);
  free(temp);
}

static int parse_and_fill_response(const PluginCommand *cmd,
                                   PluginResponse *resp, char *buffer,
                                   size_t read_len) {
  size_t comma_count = 0;
  for (size_t i = 0; i < read_len; i++) {
    if (buffer[i] == ',') {
      comma_count++;
    }
  }

  if (comma_count == 0) {
    Variable var = {0};
    parse_single_token(buffer, &var);
    plugin_response_push(resp, &var);
    return 0;
  }

  size_t token_count = comma_count + 1;
  char **tokens = malloc(token_count * sizeof(char *));
  char *buf_copy = strdup(buffer);
  size_t idx = 0;
  char *tok = strtok(buf_copy, ",");
  while (tok != NULL && idx < token_count) {
    tokens[idx++] = strdup(tok);
    tok = strtok(NULL, ",");
  }
  token_count = idx;
  free(buf_copy);

  bool all_numeric = true;
  Variable *vars = malloc(token_count * sizeof(Variable));
  for (size_t i = 0; i < token_count; i++) {
    memset(&vars[i], 0, sizeof(Variable));
    parse_single_token(tokens[i], &vars[i]);
    if (vars[i].type != PARAM_TYPE_INT64 && vars[i].type != PARAM_TYPE_DOUBLE) {
      all_numeric = false;
    }
  }

  if (all_numeric) {
    void *shm_ptr = NULL;
    const char *buf_id = data_manager_create_buffer_zero_copy(
        g_state.instrument_name, cmd->id, INST_DATA_FLOAT32, token_count,
        &shm_ptr);

    if (!buf_id || !shm_ptr) {
      VISA_LOG_ERROR("Buffer allocation failed (%zu)", token_count);
      for (size_t i = 0; i < token_count; i++) {
        free(tokens[i]);
      }
      free(tokens);
      free(vars);
      return -1;
    }

    float *out = (float *)shm_ptr;
    for (size_t i = 0; i < token_count; i++) {
      if (vars[i].type == PARAM_TYPE_INT64) {
        out[i] = (float)vars[i].value.i64_val;
      } else {
        out[i] = (float)vars[i].value.d_val;
      }
    }

    Variable var = {0};
    snprintf(var.name, sizeof(var.name), "value");
    var.type = PARAM_TYPE_BUFFER;
    snprintf(var.value.str_val, sizeof(var.value.str_val), "%s", buf_id);

    VISA_LOG_DEBUG("Parsed %zu elements -> %s", token_count, buf_id);
    plugin_response_push(resp, &var);
  } else {
    for (size_t i = 0; i < token_count; i++) {
      plugin_response_push(resp, &vars[i]);
    }
  }

  for (size_t i = 0; i < token_count; i++) {
    free(tokens[i]);
  }
  free(tokens);
  free(vars);
  return 0;
}

#define VISA_READ_POLL_MS 100
uint8_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  if (!g_state.initialized) {
    VISA_LOG_ERROR("Not initialized VISA plugin");
    return 1;
  }

  viSetAttribute(g_state.instrument, VI_ATTR_TMO_VALUE, VISA_READ_POLL_MS);

  char cmd_buf[1024];
  snprintf(cmd_buf, sizeof(cmd_buf), "%s%s", cmd->command,
           g_state.termination_char);

  ViUInt32 written = 0;
  ViStatus write_status =
      viWrite(g_state.instrument, (ViBuf)cmd_buf, strlen(cmd_buf), &written);
  if (write_status < VI_SUCCESS) {
    VISA_LOG_ERROR("Write failed: 0x%08X", write_status);
    return 1;
  }

  bool expects_response = (strchr(cmd->command, '?') != NULL);
  if (!expects_response) {
    VISA_LOG_DEBUG("Write successful (no response expected)");
    return 0;
  }

  char *buffer = NULL;
  size_t read_len = 0;
  uint8_t rc = 1;

  if (visa_read_buffer(&buffer, &read_len, cmd->timeout_ms) == 0) {
    if (parse_and_fill_response(cmd, resp, buffer, read_len) == 0) {
      rc = 0;
    }
  } else {
    VISA_LOG_ERROR("VISA read failed");
  }

  free(buffer);
  return rc;
}

void plugin_shutdown(void) {
  if (g_state.instrument)
    viClose(g_state.instrument);

  if (g_state.default_rm)
    viClose(g_state.default_rm);

  g_state.initialized = false;
}
