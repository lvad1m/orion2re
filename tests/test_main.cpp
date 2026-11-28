#define DOCTEST_CONFIG_NO_WINDOWS_SEH
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <crtdbg.h>
#include <cstdlib>

namespace {
    // A failing test must never stop on a modal dialog ("Debug Error! abort() has been called", a CRT assert,
    // the Windows crash box): unattended runs would wait for a click on the user's screen. The messages go to
    // stderr instead, and the test fails at once. Runs before doctest's main.
    const bool g_quiet_crash_dialogs = [] {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _CALL_REPORTFAULT);
#if defined(_DEBUG)
        for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }
#endif
        return true;
    }();
} // namespace
#endif
