// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// A failed assert in a test run must print and abort, never open the MSVC dialog that waits for a click nobody
// gives (ctest would sit until its timeout). Linked into every test executable; runs before main.
#include <crtdbg.h>
#include <cstdlib>

namespace
{

struct CrtReportToStderr {
    CrtReportToStderr()
    {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
};

const CrtReportToStderr s_crtReportToStderr; // NOLINT(cert-err58-cpp) cannot throw

} // namespace
