// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - the entry points 3ds Max looks for in a plugin.

#include "plugin.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

HINSTANCE hInstance = nullptr;

namespace oscmax
{

namespace
{
std::mutex g_logMutex;

const char* logPath()
{
    static const char* path = std::getenv("OPENSCATTER_LOG");
    return (path && *path) ? path : nullptr;
}
} // namespace

bool logEnabled()
{
    return logPath() != nullptr;
}

void logLine(const char* format, ...)
{
    const char* path = logPath();
    if (!path)
        return;
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    const std::lock_guard<std::mutex> lock(g_logMutex);
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") == 0 && file)
    {
        std::fputs(text, file);
        std::fputc('\n', file);
        std::fclose(file);
    }
}

} // namespace oscmax

BOOL WINAPI DllMain(HINSTANCE module, ULONG reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        hInstance = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

__declspec(dllexport) const MCHAR* LibDescription()
{
    return _T("OpenScatter ") _T(OPENSCATTER_VERSION_STRING) _T(" - scatter for 3ds Max (Open3D)");
}

__declspec(dllexport) int LibNumberClasses()
{
    return 2;
}

__declspec(dllexport) ClassDesc* LibClassDesc(int i)
{
    switch (i)
    {
    case 0:
        return oscmax::getOpenScatterDesc();
    case 1:
        return oscmax::getOpenScatterOperatorDesc();
    default:
        return nullptr;
    }
}

__declspec(dllexport) ULONG LibVersion()
{
    return VERSION_3DSMAX;
}

// The scene notifications the Particle Flow carriers depend on must be in place from the start,
// so the plugin is never loaded on demand.
__declspec(dllexport) ULONG CanAutoDefer()
{
    return 0;
}

__declspec(dllexport) int LibInitialize()
{
    oscmax::registerCarrierNotifications();
    return TRUE;
}

__declspec(dllexport) int LibShutdown()
{
    oscmax::unregisterCarrierNotifications();
    return TRUE;
}
