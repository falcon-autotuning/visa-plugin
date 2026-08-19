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

#define MAX_TERMINATION_LEN 4
typedef struct {
  ViSession default_rm;
  ViSession instrument;
  char instrument_name[PLUGIN_MAX_STRING_LEN];
  char resource_address[PLUGIN_MAX_STRING_LEN];
  uint32_t timeout_ms;
  char termination_char[MAX_TERMINATION_LEN];
  char array_delimitter[MAX_TERMINATION_LEN];
  char multi_arg_delimitter[MAX_TERMINATION_LEN];
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
    VISA_LOG_ERROR("Could not parse the address field in the configuration");
    return 1;
  }

  snprintf(g_state.resource_address, sizeof(g_state.resource_address), "%s",
           config->address);

  g_state.timeout_ms = 0;
  const char *term = "\\n";
  const char *array_indicator = " ";
  const char *multi_arg_delimitter = ",";

  cJSON *custom_json = cJSON_Parse(config->custom);
  if (custom_json) {
    VISA_LOG_DEBUG("Custom json detected");
    g_state.timeout_ms = get_json_int(custom_json, "tout", 0);
    term = get_json_string(custom_json, "term", "\\n");
    array_indicator = get_json_string(custom_json, "arr_d", " ");
    multi_arg_delimitter = get_json_string(custom_json, "arg_d", ",");
  }
  if (strlen(term) >= MAX_TERMINATION_LEN) {
    VISA_LOG_ERROR("The selected termination for the instrument is too long");
    return 1;
  }
  if (strlen(term) == 0) {
    VISA_LOG_ERROR(
        "The selected termination for the instrument is no characters");
    return 1;
  }
  if (g_state.timeout_ms != 0) {
    VISA_LOG_DEBUG(
        "The selected custom timeout for the instrument in milliseconds is %d",
        g_state.timeout_ms);
  }

  if (strcmp(term, "\\n") == 0) {
    term = "\n";
    VISA_LOG_DEBUG("The selected termination for the instrument is newline");
  } else if (strcmp(term, "\\r") == 0) {
    term = "\r";
    VISA_LOG_DEBUG("The selected termination for the instrument is r");
  } else if (strcmp(term, "\\r\\n") == 0) {
    term = "\r\n";
    VISA_LOG_DEBUG("The selected termination for the instrument is r newline");
  } else {
    VISA_LOG_DEBUG(
        "The selected termination for the instrument is something else");
  }
  snprintf(g_state.termination_char, sizeof(g_state.termination_char), "%s",
           term);
  VISA_LOG_DEBUG("The selected array delimitter for the instrument is: '%s'",
                 array_indicator);
  snprintf(g_state.array_delimitter, sizeof(g_state.array_delimitter), "%s",
           array_indicator);
  VISA_LOG_DEBUG(
      "The selected multi-arg delimitter for the instrument is: '%s'",
      multi_arg_delimitter);
  snprintf(g_state.multi_arg_delimitter, sizeof(g_state.multi_arg_delimitter),
           "%s", multi_arg_delimitter);
  if (strcmp(multi_arg_delimitter, array_indicator) == 0) {
    VISA_LOG_WARN(
        "The multi-output delimitter is the same as the array delimitter, this "
        "may cause issues with parsing");
  }
  if (custom_json) {
    cJSON_Delete(custom_json);
  }

  if (viOpenDefaultRM(&g_state.default_rm) < VI_SUCCESS) {
    VISA_LOG_ERROR("Unable to start default rm for VISA");
    return 1;
  }

  if (viOpen(g_state.default_rm, g_state.resource_address, VI_NO_LOCK,
             g_state.timeout_ms, &g_state.instrument) < VI_SUCCESS) {
    VISA_LOG_ERROR("Unable to lock instrument and check the instrument state");
    return 1;
  }

  g_state.initialized = true;
  return 0;
}

typedef enum { ARRAY_VALUE_INT64, ARRAY_VALUE_DOUBLE } ArrayValueType;

typedef struct {
  ArrayValueType type;
  union {
    int64_t i64_val;
    double d_val;
  };
} ArrayValue;

static size_t parse_array(char *buf, ArrayValue *out, size_t max,
                          const char *delimiter) {
  size_t count = 0;

  char *saveptr = NULL;
  char *token = strtok_r(buf, delimiter, &saveptr);

  while (token != NULL && count < max) {
    char *end = NULL;

    long long i = strtoll(token, &end, 10);

    if (end != token && *end == '\0') {
      out[count].type = ARRAY_VALUE_INT64;
      out[count].i64_val = i;
    } else {
      double d = strtod(token, &end);

      if (end == token || *end != '\0') {
        break; // invalid token
      }

      out[count].type = ARRAY_VALUE_DOUBLE;
      out[count].d_val = d;
    }

    count++;
    token = strtok_r(NULL, delimiter, &saveptr);
  }

  return count;
}

static int visa_read_buffer(char **out_buf, size_t *out_len,
                            uint32_t timeout_ms) {
  char *buffer = malloc(MAX_READ_SIZE);
  if (!buffer) {
    VISA_LOG_ERROR("Failed to allocate read buffer");
    return 1;
  }

  uint64_t start_time = get_time_ms();

  size_t total_read = 0;
  size_t term_len = strlen(g_state.termination_char);

  while (total_read < (MAX_READ_SIZE - 1)) {
    uint64_t elapsed = get_time_ms() - start_time;

    if (elapsed >= timeout_ms) {
      VISA_LOG_ERROR("Overall VISA timeout reached (%u ms)", timeout_ms);
      free(buffer);
      return 1;
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

      if (total_read >= term_len &&
          memcmp(buffer + total_read - term_len, g_state.termination_char,
                 term_len) == 0) {
        VISA_LOG_TRACE("Termination detected, total_read=%zu", total_read);
        total_read -= term_len;
        buffer[total_read] = '\0';
        VISA_LOG_TRACE("Final buffer: '%s'", buffer);
        VISA_LOG_TRACE("Final buffer length: %zu", total_read);

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
      return 1;
    }
  }

  VISA_LOG_ERROR("Read buffer exceeded maximum size");
  free(buffer);
  return 1;
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

typedef enum { DELIM_MULTI_ARG, DELIM_ARRAY, DELIM_INVALID } DelimiterType;

typedef struct {
  size_t position;
  DelimiterType type;
} DelimiterLocation;
static size_t delimiter_length(DelimiterType type) {
  return (type == DELIM_MULTI_ARG) ? strlen(g_state.multi_arg_delimitter)
                                   : strlen(g_state.array_delimitter);
}

static int parse_and_fill_response(const PluginCommand *cmd,
                                   PluginResponse *resp, char *buffer,
                                   size_t buffer_length) {
  // First sort our the delimiters in the buffer, then we can parse the tokens
  size_t delimiter_count = 0;
  VISA_LOG_TRACE("The multi-arg delimiter is: '%s'",
                 g_state.multi_arg_delimitter);
  size_t multi_len = delimiter_length(DELIM_MULTI_ARG);
  VISA_LOG_TRACE("Multi-arg delimiter length: %zu", multi_len);
  VISA_LOG_TRACE("The array delimiter is: '%s'", g_state.array_delimitter);
  size_t array_len = delimiter_length(DELIM_ARRAY);
  VISA_LOG_TRACE("Array delimiter length: %zu", array_len);
  bool in_string = false;
  for (size_t i = 0; i < buffer_length;) {
    if (buffer[i] == '"' || buffer[i] == '\'') {
      in_string = !in_string;
      VISA_LOG_TRACE("Toggled in_string to %d at position %zu", in_string, i);
      i++;
      continue;
    }
    if (in_string) {
      VISA_LOG_TRACE("Inside string, skipping position %zu", i);
      i++;
      continue;
    }
    if (multi_len > 0 && i + multi_len <= buffer_length &&
        strncmp(buffer + i, g_state.multi_arg_delimitter, multi_len) == 0) {
      VISA_LOG_TRACE("Found multi-arg delimiter at position %zu", i);
      delimiter_count++;
      i += multi_len;
      continue;
    }
    if (array_len > 0 && i + array_len <= buffer_length &&
        strncmp(buffer + i, g_state.array_delimitter, array_len) == 0) {
      VISA_LOG_TRACE("Found array delimiter at position %zu", i);
      delimiter_count++;
      i += array_len;
      continue;
    }
    VISA_LOG_TRACE("No delimiter at position %zu", i);

    i++;
  }
  VISA_LOG_TRACE("Found %zu delimiters in the buffer", delimiter_count);

  // Now we can fill the locations of delimitters in the buffer
  DelimiterLocation *locations =
      malloc(delimiter_count * sizeof(DelimiterLocation));
  size_t idx = 0;
  for (size_t i = 0; i < buffer_length;) {
    if (buffer[i] == '"' || buffer[i] == '\'') {
      in_string = !in_string;
      VISA_LOG_TRACE("Toggled in_string to %d at position %zu", in_string, i);
      i++;
      continue;
    }
    if (in_string) {
      VISA_LOG_TRACE("Inside string, skipping position %zu", i);
      i++;
      continue;
    }
    if (multi_len > 0 && i + multi_len <= buffer_length &&
        strncmp(buffer + i, g_state.multi_arg_delimitter, multi_len) == 0) {

      locations[idx++] =
          (DelimiterLocation){.position = i, .type = DELIM_MULTI_ARG};

      i += multi_len;
      continue;
    }
    if (array_len > 0 && i + array_len <= buffer_length &&
        strncmp(buffer + i, g_state.array_delimitter, array_len) == 0) {

      locations[idx++] =
          (DelimiterLocation){.position = i, .type = DELIM_ARRAY};

      i += array_len;
      continue;
    }
    i++;
  }
  // Now we can filter out obvious invalid delimitters
  size_t valid_count = delimiter_count;
  for (ssize_t i = (ssize_t)valid_count - 1; i >= 0; --i) {
    size_t len = delimiter_length(locations[i].type);

    // delimiter at beginning
    if (locations[i].position == 0) {
      locations[i].type = DELIM_INVALID;
      continue;
    }

    // delimiter at end
    if (locations[i].position + len >= buffer_length) {
      locations[i].type = DELIM_INVALID;
      continue;
    }

    // delimiter immediately follows previous delimiter
    if (i > 0) {
      size_t prev_end =
          locations[i - 1].position + delimiter_length(locations[i - 1].type);

      if (prev_end == locations[i].position) {
        // discard rightmost delimiter
        locations[i].type = DELIM_INVALID;
      }
    }
  }
  VISA_LOG_TRACE("Found %zu valid delimiters in the buffer", valid_count);

  size_t segment_start = 0; // indexes the segment of the buffer
  size_t location_idx = 0;  // indexes the locations array
  while (segment_start < buffer_length) {
    // default values
    size_t segment_end = buffer_length;
    ssize_t next_multi_idx = -1;

    // Examine delimiters inside this segment
    size_t array_delimitter_count = 0;
    for (size_t i = location_idx; i < delimiter_count; ++i) {
      if (locations[i].type == DELIM_INVALID) {
        continue;
      }
      if (locations[i].type == DELIM_ARRAY) {
        array_delimitter_count++;
        continue;
      }
      // Find next multi-arg delimiter
      segment_end = locations[i].position;
      next_multi_idx = i;
      break;
    }
    VISA_LOG_DEBUG("Segment start=%zu end=%zu array_delimitter_count=%zu",
                   segment_start, segment_end, array_delimitter_count);

    // Compute the length of each segment, excluding the multi-arg delimiter at
    // the beginning of the segment
    size_t segment_len = segment_end - segment_start;
    if (segment_len == 0) {
      VISA_LOG_ERROR("No segment found with a length");
      free(locations);
      return 1;
    }

    // Isolate the segment from the buffer for parsing
    char *segment = malloc(segment_len + 1);
    memcpy(segment, buffer + segment_start, segment_len);
    segment[segment_len] = '\0';

    // Create the variable to hold the parsed value
    Variable var = {0};
    if (array_delimitter_count == 0) {
      VISA_LOG_TRACE("Parsing single token: '%s'", segment);

      parse_single_token(segment, &var);
      plugin_response_push(resp, &var);

    } else {
      VISA_LOG_TRACE("Parsing array segment: '%s'", segment);
      // Increment by one since the number of elements is one more than the
      // number of delimiters
      size_t array_count = array_delimitter_count + 1;

      ArrayValue *values = malloc(array_count * sizeof(ArrayValue));
      size_t count =
          parse_array(segment, values, array_count, g_state.array_delimitter);
      bool all_int64 = true;
      for (size_t k = 0; k < count; ++k) {
        if (values[k].type != ARRAY_VALUE_INT64) {
          all_int64 = false;
          break;
        }
      }
      uint32_t data_type = all_int64 ? INST_DATA_INT64 : INST_DATA_FLOAT64;
      void *shm_ptr = NULL;
      const char *buf_id = data_manager_create_buffer_zero_copy(
          g_state.instrument_name, cmd->id, data_type, count, &shm_ptr);
      if (!buf_id || !shm_ptr) {
        VISA_LOG_ERROR("Buffer allocation failed (%zu)", count);
        free(segment);
        free(locations);
        free(values);
        return 1;
      }

      if (all_int64) {
        int64_t *out = (int64_t *)shm_ptr;
        for (size_t k = 0; k < count; ++k) {
          out[k] = values[k].i64_val;
        }
      } else {
        double *out = (double *)shm_ptr;
        for (size_t k = 0; k < count; ++k) {
          out[k] = (values[k].type == ARRAY_VALUE_INT64)
                       ? (double)values[k].i64_val
                       : values[k].d_val;
        }
      }
      snprintf(var.name, sizeof(var.name), "value");
      var.type = PARAM_TYPE_BUFFER;
      snprintf(var.value.str_val, sizeof(var.value.str_val), "%s", buf_id);

      VISA_LOG_DEBUG("Parsed %zu array elements -> %s", count, buf_id);
      plugin_response_push(resp, &var);
      free(values);
    }
    free(segment);
    // If next_multi_idx is -1, then we are at the last segment and segment_end
    // is the end of the buffer
    if (next_multi_idx == -1) {
      break;
    }
    // Fix the two different counters, one for the buffer and one for the
    // locations array
    segment_start = locations[next_multi_idx].position +
                    strlen(g_state.multi_arg_delimitter);
    location_idx = next_multi_idx + 1;
  }

  VISA_LOG_DEBUG("Parsed %zu elements", delimiter_count);
  free(locations);
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

  char *buffer = NULL;
  size_t read_len = 0;
  VISA_LOG_DEBUG("Preparing response for command %s", cmd->command);
  uint32_t timeout_ms =
      (g_state.timeout_ms > 0) ? g_state.timeout_ms : cmd->timeout_ms;
  if (cmd->is_query) {
    VISA_LOG_DEBUG("Is a query, awaiting %d ms for the response", timeout_ms);
    if (visa_read_buffer(&buffer, &read_len, timeout_ms) != 0) {
      VISA_LOG_ERROR("VISA read failed");
      free(buffer);
      return 1;
    }

    if (parse_and_fill_response(cmd, resp, buffer, read_len) != 0) {
      VISA_LOG_ERROR("VISA parse and response filling failed");
      free(buffer);
      return 1;
    }
  }
  VISA_LOG_DEBUG("Finished command %s", cmd->command);
  free(buffer);
  return 0;
}

void plugin_shutdown(void) {
  if (g_state.instrument)
    viClose(g_state.instrument);

  if (g_state.default_rm)
    viClose(g_state.default_rm);

  g_state.initialized = false;
}
