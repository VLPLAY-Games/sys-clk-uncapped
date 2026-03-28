/*
 * --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

typedef enum {
    SysClkConfigValue_PollingIntervalMs = 0,
    SysClkConfigValue_UnlockGpuMariko = 1,
    SysClkConfigValue_UnlockGpuErista = 2,
    SysClkConfigValue_OnlyOnCharging = 3,
    SysClkConfigValue_EnumMax,
} SysClkConfigValue;

typedef struct {
    uint64_t values[16];
} SysClkConfigValueList;

static inline const char* sysclkFormatConfigValue(SysClkConfigValue val, bool pretty)
{
    switch(val)
    {
        case SysClkConfigValue_PollingIntervalMs:
            return pretty ? "Polling Interval (ms)" : "poll_interval_ms";
        case SysClkConfigValue_UnlockGpuMariko:
            return pretty ? "Unlock GPU (Mariko/OLED/Lite)" : "unlock_gpu_mariko";
        case SysClkConfigValue_UnlockGpuErista:
            return pretty ? "Unlock GPU (Erista/V1)" : "unlock_gpu_erista";
        case SysClkConfigValue_OnlyOnCharging:
            return pretty ? "Only while charging" : "only_on_charging";
        default:
            return NULL;
    }
}

static inline uint64_t sysclkDefaultConfigValue(SysClkConfigValue val)
{
    switch(val)
    {
        case SysClkConfigValue_PollingIntervalMs:
            return 300ULL;
        case SysClkConfigValue_UnlockGpuMariko:
        case SysClkConfigValue_UnlockGpuErista:
        case SysClkConfigValue_OnlyOnCharging:
            return 0ULL;
        default:
            return 0ULL;
    }
}

static inline uint64_t sysclkValidConfigValue(SysClkConfigValue val, uint64_t input)
{
    switch(val)
    {
        case SysClkConfigValue_PollingIntervalMs:
            return input > 0;
        case SysClkConfigValue_UnlockGpuMariko:
        case SysClkConfigValue_UnlockGpuErista:
        case SysClkConfigValue_OnlyOnCharging:
            return input <= 1;
        default:
            return false;
    }
}