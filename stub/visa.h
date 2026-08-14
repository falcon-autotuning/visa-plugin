#pragma once

#include <stdint.h>

typedef int32_t ViStatus;
typedef void *ViSession;
typedef uint32_t ViUInt32;
typedef uint32_t ViAttr;
typedef unsigned char *ViBuf;

#define VI_SUCCESS 0
#define VI_SUCCESS_MAX_CNT 0x3FFF0006

#define VI_NO_LOCK 0
#define VI_ATTR_TMO_VALUE 0x3FFF001A

#define VI_ERROR_TMO 10

ViStatus viOpenDefaultRM(ViSession *rm);
ViStatus viOpen(ViSession rm, const char *addr, uint32_t mode, uint32_t timeout,
                ViSession *instr);

ViStatus viSetAttribute(ViSession instr, ViAttr attr, uint32_t val);

ViStatus viWrite(ViSession instr, ViBuf buf, ViUInt32 count, ViUInt32 *written);

ViStatus viRead(ViSession instr, ViBuf buf, ViUInt32 count, ViUInt32 *read);

ViStatus viClose(ViSession sess);
