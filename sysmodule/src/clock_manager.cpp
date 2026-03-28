/*
 * --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */

#include "clock_manager.h"
#include <cstring>
#include "file_utils.h"
#include "board.h"
#include "process_management.h"
#include "errors.h"

ClockManager::ClockManager()
{
    this->config = Config::CreateDefault();

    this->context = new SysClkContext;
    this->context->applicationId = 0;
    this->context->profile = SysClkProfile_Handheld;
    this->context->enabled = false;
    for(unsigned int module = 0; module < SysClkModule_EnumMax; module++)
    {
        this->context->freqs[module] = 0;
        this->context->realFreqs[module] = 0;
        this->context->overrideFreqs[module] = 0;
        this->RefreshFreqTableRow((SysClkModule)module);
    }

    this->running = false;
}

ClockManager::~ClockManager()
{
    delete this->config;
    delete this->context;
}

SysClkContext ClockManager::GetCurrentContext()
{
    std::scoped_lock lock{this->contextMutex};
    return *this->context;
}

Config* ClockManager::GetConfig()
{
    return this->config;
}

void ClockManager::SetRunning(bool running)
{
    this->running = running;
}

bool ClockManager::Running()
{
    return this->running;
}

void ClockManager::GetFreqList(SysClkModule module, std::uint32_t* list, std::uint32_t maxCount, std::uint32_t* outCount)
{
    ASSERT_ENUM_VALID(SysClkModule, module);

    *outCount = std::min(maxCount, this->freqTable[module].count);
    memcpy(list, &this->freqTable[module].list[0], *outCount * sizeof(this->freqTable[0].list[0]));
}

bool ClockManager::IsAssignableHz(SysClkModule module, std::uint32_t hz)
{
    switch(module)
    {
        case SysClkModule_CPU:
            return hz >= 612000000;
        case SysClkModule_MEM:
            return hz == 204000000 || hz >= 665600000;
        default:
            return true;
    }
}

std::uint32_t ClockManager::GetMaxAllowedHz(SysClkModule module, SysClkProfile profile)
{
    if (module == SysClkModule_GPU)
    {
        bool onlyCharging = this->config->GetConfigValue(SysClkConfigValue_OnlyOnCharging) != 0;
        bool isCharging = (profile >= SysClkProfile_Charging);
        SysClkSocType soc = Board::GetSocType();

        // 1. If "Only on charging" is enabled but not charging — stock only
        if (onlyCharging && !isCharging) {
            return (soc == SysClkSocType_Mariko) ? 614400000 : 460800000;
        }

        // 2. Mariko (V2, Lite, OLED) logic
        if (soc == SysClkSocType_Mariko && this->config->GetConfigValue(SysClkConfigValue_UnlockGpuMariko)) {
            return 1267200000; 
        }

        // 3. Erista (V1) logic
        if (soc == SysClkSocType_Erista && this->config->GetConfigValue(SysClkConfigValue_UnlockGpuErista)) {
            return 921600000;
        }

        // 4. Default stock limits
        return isCharging ? 768000000 : (soc == SysClkSocType_Mariko ? 614400000 : 460800000);
    }
    return 0;
}

std::uint32_t ClockManager::GetNearestHz(SysClkModule module, std::uint32_t inHz, std::uint32_t maxHz)
{
    std::uint32_t* freqs = &this->freqTable[module].list[0];
    size_t count = this->freqTable[module].count - 1;

    size_t i = 0;
    while(i < count)
    {
        if (maxHz > 0 && freqs[i] >= maxHz)
        {
            break;
        }

        if (inHz <= ((std::uint64_t)freqs[i] + freqs[i + 1]) / 2)
        {
            break;
        }

        i++;
    }

    return freqs[i];
}

void ClockManager::RefreshFreqTableRow(SysClkModule module)
{
    std::scoped_lock lock{this->contextMutex};

    std::uint32_t freqs[SYSCLK_FREQ_LIST_MAX];
    std::uint32_t count;

    Board::GetFreqList(module, &freqs[0], SYSCLK_FREQ_LIST_MAX, &count);

    std::uint32_t* hz = &this->freqTable[module].list[0];
    this->freqTable[module].count = 0;
    for(std::uint32_t i = 0; i < count; i++)
    {
        if(!this->IsAssignableHz(module, freqs[i]))
        {
            continue;
        }

        *hz = freqs[i];
        this->freqTable[module].count++;
        hz++;
    }
}

void ClockManager::Tick()
{
    std::scoped_lock lock{this->contextMutex};
    if (this->RefreshContext() || this->config->Refresh())
    {
        std::uint32_t targetHz = 0;
        std::uint32_t maxHz = 0;
        std::uint32_t nearestHz = 0;
        for (unsigned int module = 0; module < SysClkModule_EnumMax; module++)
        {
            targetHz = this->context->overrideFreqs[module];

            if(!targetHz)
            {
                targetHz = this->config->GetAutoClockHz(this->context->applicationId, (SysClkModule)module, this->context->profile);
            }

            if (targetHz)
            {
                maxHz = this->GetMaxAllowedHz((SysClkModule)module, this->context->profile);
                nearestHz = this->GetNearestHz((SysClkModule)module, targetHz, maxHz);

                if (nearestHz != this->context->freqs[module] && this->context->enabled)
                {
                    Board::SetHz((SysClkModule)module, nearestHz);
                    this->context->freqs[module] = nearestHz;
                }
            }
        }
    }
}

void ClockManager::WaitForNextTick()
{
    svcSleepThread(this->GetConfig()->GetConfigValue(SysClkConfigValue_PollingIntervalMs) * 1000000ULL);
}

bool ClockManager::RefreshContext()
{
    bool hasChanged = false;

    bool enabled = this->GetConfig()->Enabled();
    if(enabled != this->context->enabled)
    {
        this->context->enabled = enabled;
        hasChanged = true;
    }

    std::uint64_t applicationId = ProcessManagement::GetCurrentApplicationId();
    if (applicationId != this->context->applicationId)
    {
        this->context->applicationId = applicationId;
        hasChanged = true;
    }

    SysClkProfile profile = Board::GetProfile();
    if (profile != this->context->profile)
    {
        this->context->profile = profile;
        hasChanged = true;
    }

    // restore clocks to stock values on app or profile change
    if(hasChanged)
    {
        Board::ResetToStock();
        this->WaitForNextTick();
    }

    std::uint32_t hz = 0;
    for (unsigned int module = 0; module < SysClkModule_EnumMax; module++)
    {
        hz = Board::GetHz((SysClkModule)module);
        if (hz != 0 && hz != this->context->freqs[module])
        {
            this->context->freqs[module] = hz;
            hasChanged = true;
        }

        hz = this->GetConfig()->GetOverrideHz((SysClkModule)module);
        if (hz != this->context->overrideFreqs[module])
        {
            this->context->overrideFreqs[module] = hz;
            hasChanged = true;
        }
    }

    // temperatures, power, real freqs, ram load – update context without logging
    for (unsigned int sensor = 0; sensor < SysClkThermalSensor_EnumMax; sensor++)
    {
        this->context->temps[sensor] = Board::GetTemperatureMilli((SysClkThermalSensor)sensor);
    }

    for (unsigned int sensor = 0; sensor < SysClkPowerSensor_EnumMax; sensor++)
    {
        this->context->power[sensor] = Board::GetPowerMw((SysClkPowerSensor)sensor);
    }

    for (unsigned int module = 0; module < SysClkModule_EnumMax; module++)
    {
        this->context->realFreqs[module] = Board::GetRealHz((SysClkModule)module);
    }

    for (unsigned int loadSource = 0; loadSource < SysClkRamLoad_EnumMax; loadSource++)
    {
        this->context->ramLoad[loadSource] = Board::GetRamLoad((SysClkRamLoad)loadSource);
    }

    return hasChanged;
}