#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>

static FILE* g_log = nullptr;

void Log_Init(const char* filename)
{
    if (g_log) return;
    fopen_s(&g_log, filename, "w");
    if (g_log) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(g_log, "[%02d:%02d:%02d] log opened\n", st.wHour, st.wMinute, st.wSecond);
        fflush(g_log);
    }
}

void Log_Close()
{
    if (g_log) { fclose(g_log); g_log = nullptr; }
}

void Log_Printf(const char* fmt, ...)
{
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}
