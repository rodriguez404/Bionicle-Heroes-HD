#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static FILE* g_log = nullptr;

void LogOpen(const char* path)
{
    g_log = std::fopen(path, "w");
}

void Log(const char* fmt, ...)
{
    if (!g_log)
        return;
    // One fputs per line keeps lines whole when the watchdog thread logs too.
    char line[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = std::snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond,
                          t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    if (n > 0 && n < static_cast<int>(sizeof(line)) - 1)
        std::vsnprintf(line + n, sizeof(line) - n - 1, fmt, args);
    va_end(args);
    std::strcat(line, "\n");
    std::fputs(line, g_log);
    std::fflush(g_log);
}
