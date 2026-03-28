/*
 * --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */

#include "file_utils.h"
#include <nxExt.h>

static std::atomic_bool g_has_initialized = false;

extern "C" void __libnx_init_time(void);

static void _FileUtils_InitializeThreadFunc(void* args)
{
    FileUtils::Initialize();
}

bool FileUtils::IsInitialized()
{
    return g_has_initialized;
}

void FileUtils::InitializeAsync()
{
    Thread initThread = {0};
    threadCreate(&initThread, _FileUtils_InitializeThreadFunc, NULL, NULL, 0x4000, 0x15, 0);
    threadStart(&initThread);
}

Result FileUtils::Initialize()
{
    Result rc = 0;

    if (R_SUCCEEDED(rc))
    {
        rc = timeInitialize();
    }

    __libnx_init_time();
    timeExit();

    if (R_SUCCEEDED(rc))
    {
        rc = fsInitialize();
    }

    if (R_SUCCEEDED(rc))
    {
        rc = fsdevMountSdmc();
    }

    if (R_SUCCEEDED(rc))
    {
        g_has_initialized = true;
    }

    return rc;
}

void FileUtils::Exit()
{
    if (!g_has_initialized)
    {
        return;
    }

    g_has_initialized = false;

    fsdevUnmountAll();
    fsExit();
}