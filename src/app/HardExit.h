#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdlib>

#if defined (_WIN32)
extern "C"
{
    __declspec (dllimport) void* __stdcall GetCurrentProcess();
    __declspec (dllimport) int __stdcall TerminateProcess (void* process, unsigned int exitCode);
}
#else
 #include <unistd.h>
#endif

namespace ff
{
/** Ends the process at once, without running static destructors or unloading plugin libraries. Used as the very
    last step of a clean shutdown (everything is saved and closed by then): some plugins and audio drivers hang
    while their library is unloaded, which would leave the app in the task manager after its window has gone. */
[[noreturn]] inline void hardExit (int code)
{
   #if defined (_WIN32)
    TerminateProcess (GetCurrentProcess(), (unsigned int) code);
    std::abort();   // not reached
   #else
    _exit (code);
   #endif
}

/** Started when shutdown begins: if closing down takes longer than `seconds` (a plugin or driver that never
    returns), the process is ended anyway. Projects and settings are saved before this is armed. */
inline void armShutdownWatchdog (int seconds)
{
    std::thread ([seconds] {
        std::this_thread::sleep_for (std::chrono::seconds (seconds));
        hardExit (0);
    }).detach();
}
} // namespace ff
