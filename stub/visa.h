#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int32_t ViStatus;
typedef void *ViSession;
typedef uint32_t ViUInt32;
typedef uint32_t ViAttr;
typedef unsigned char *ViBuf;
typedef char ViChar;
typedef void *ViObject;

#define VI_SUCCESS 1
#define VI_NULL 0
#define VI_EXCLUSIVE_LOCK 4
#define VI_SUCCESS_MAX_CNT 0x3FFF0006
#define VI_IO_IN_BUF_DISCARD 5

#define VI_NO_LOCK 0
#define VI_ATTR_TMO_VALUE 0x3FFF001A
#define VI_ATTR_ASRL_BAUD 0x3FFF001B

#define VI_ERROR_TMO 10

ViStatus viOpenDefaultRM(ViSession *rm);

ViStatus viOpen(ViSession rm, const char *addr, uint32_t mode, uint32_t timeout,
                ViSession *instr);

ViStatus viSetAttribute(ViSession instr, ViAttr attr, uint32_t val);

ViStatus viWrite(ViSession instr, ViBuf buf, ViUInt32 count, ViUInt32 *written);

ViStatus viRead(ViSession instr, ViBuf buf, ViUInt32 count, ViUInt32 *read);

ViStatus viClose(ViSession sess);

ViStatus viFlush(ViSession instr, ViAttr attr);

ViStatus viStatusDesc(ViSession rm, ViStatus status, ViChar desc[]);

/* Test helpers */
void visa_stub_set_response(const char *resp);
void visa_stub_set_response_delay(uint32_t delay_ms);
void visa_stub_set_chunk_size(size_t chunk_size);
void visa_stub_reset(void);
void visa_stub_set_status_desc(const char *desc_text);
