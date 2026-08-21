#include "visa.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static const char *g_stub_response = "1.0,2.0,3.0\n";
static uint32_t g_read_timeout_ms = 2000;
static uint32_t g_response_delay_ms = 0;
static size_t g_chunk_size = SIZE_MAX;

static size_t g_offset = 0;
static uint64_t g_first_read_time = 0;

static uint64_t now_ms(void) {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
#endif
}

void visa_stub_set_response(const char *resp) {
  g_stub_response = resp;

  g_offset = 0;
  g_first_read_time = 0;
}

void visa_stub_set_response_delay(uint32_t delay_ms) {
  g_response_delay_ms = delay_ms;
}

void visa_stub_set_chunk_size(size_t chunk_size) { g_chunk_size = chunk_size; }

void visa_stub_reset(void) {
  g_stub_response = "1.0,2.0,3.0\n";

  g_response_delay_ms = 0;
  g_chunk_size = SIZE_MAX;

  g_offset = 0;
  g_first_read_time = 0;
}

ViStatus viOpenDefaultRM(ViSession *rm) {
  *rm = (ViSession)1;
  return VI_SUCCESS;
}

ViStatus viOpen(ViSession rm, const char *addr, uint32_t mode, uint32_t timeout,
                ViSession *instr) {
  (void)rm;
  (void)mode;
  (void)timeout;

  *instr = (ViSession)2;

  fprintf(stderr, "[VISA_STUB] Open: %s\n", addr);

  return VI_SUCCESS;
}

ViStatus viSetAttribute(ViSession instr, ViAttr attr, uint32_t val) {
  (void)instr;

  if (attr == VI_ATTR_TMO_VALUE) {
    g_read_timeout_ms = val;
  }

  return VI_SUCCESS;
}

ViStatus viWrite(ViSession instr, ViBuf buf, ViUInt32 count,
                 ViUInt32 *written) {
  (void)instr;

  *written = count;

  /*
   * Each command starts a new response sequence.
   */
  g_offset = 0;
  g_first_read_time = 0;

  fprintf(stderr, "[VISA_STUB] Write: %.*s\n", (int)count, (const char *)buf);

  return VI_SUCCESS;
}

ViStatus viRead(ViSession instr, ViBuf buf, ViUInt32 count, ViUInt32 *read) {
  (void)instr;

  if (g_first_read_time == 0) {
    g_first_read_time = now_ms();
  }

  uint64_t elapsed = now_ms() - g_first_read_time;

  if (elapsed < g_response_delay_ms) {
    uint64_t wait_needed = g_response_delay_ms - elapsed;

    /*
     * Simulate instrument not having data ready yet.
     */
    if (wait_needed > g_read_timeout_ms) {
      fprintf(stderr, "[VISA_STUB] Read timeout (%llu/%u ms)\n",
              (unsigned long long)elapsed, g_response_delay_ms);
      *read = 0;
      struct timespec ts = {.tv_sec = (long)(g_read_timeout_ms / 1000),
                            .tv_nsec = (g_read_timeout_ms % 1000) * 1000000L};
      nanosleep(&ts, NULL);
      return VI_ERROR_TMO;
    }

    /* wait only until data becomes available */
    struct timespec ts = {.tv_sec = (long)(wait_needed / 1000),
                          .tv_nsec = (wait_needed % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }

  size_t response_len = strlen(g_stub_response);

  if (g_offset >= response_len) {
    *read = 0;

    return VI_ERROR_TMO;
  }

  size_t remaining = response_len - g_offset;

  size_t len = remaining;

  if (len > g_chunk_size) {
    len = g_chunk_size;
  }

  if (len > count) {
    len = count;
  }

  memcpy(buf, g_stub_response + g_offset, len);

  g_offset += len;

  *read = (ViUInt32)len;

  fprintf(stderr, "[VISA_STUB] Read chunk=%zu offset=%zu/%zu\n", len, g_offset,
          response_len);

  /*
   * More data remains.
   */
  if (g_offset < response_len) {
    return VI_SUCCESS_MAX_CNT;
  }
  g_first_read_time = 0; // reset for next read

  return VI_SUCCESS;
}

ViStatus viClose(ViSession sess) {
  (void)sess;
  return VI_SUCCESS;
}

static char global_status_desc[256] = "Default mock error description";

void visa_stub_set_status_desc(const char *desc_text) {
  if (desc_text) {
    strncpy(global_status_desc, desc_text, sizeof(global_status_desc) - 1);
    global_status_desc[sizeof(global_status_desc) - 1] = '\0';
  }
}

ViStatus viStatusDesc(ViSession rm, ViStatus status, ViChar desc[]) {
  // Fill the passed buffer with your mock error description string
  if (desc != NULL) {
    strcpy(desc, global_status_desc);
  }
  return VI_SUCCESS;
}
