#include "visa.h"

#include <stdio.h>
#include <string.h>
static const char *g_stub_response = "1.0,2.0,3.0\n";

void visa_stub_set_response(const char *resp) { g_stub_response = resp; }
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
  (void)attr;
  (void)val;
  return VI_SUCCESS;
}

ViStatus viWrite(ViSession instr, ViBuf buf, ViUInt32 count,
                 ViUInt32 *written) {
  (void)instr;

  *written = count;

  fprintf(stderr, "[VISA_STUB] Write: %.*s\n", (int)count, (const char *)buf);

  return VI_SUCCESS;
}

ViStatus viRead(ViSession instr, ViBuf buf, ViUInt32 count, ViUInt32 *read) {
  (void)instr;

  const char *response = g_stub_response;

  size_t len = strlen(response);
  if (len > count) {
    len = count;
  }

  memcpy(buf, response, len);

  *read = (ViUInt32)len;

  fprintf(stderr, "[VISA_STUB] Read -> %s\n", response);

  return VI_SUCCESS;
}

ViStatus viClose(ViSession sess) {
  (void)sess;
  return VI_SUCCESS;
}
