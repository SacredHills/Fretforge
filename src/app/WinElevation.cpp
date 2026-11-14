#include "WinElevation.h"
#if defined (_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#endif

namespace ff
{
bool isRunningElevated()
{
#if defined (_WIN32)
    HANDLE token = nullptr;
    bool elevated = false;
    if (OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION e {};
        DWORD size = 0;
        if (GetTokenInformation (token, TokenElevation, &e, sizeof (e), &size)) elevated = e.TokenIsElevated != 0;
        CloseHandle (token);
    }
    return elevated;
#else
    return false;
#endif
}
}
