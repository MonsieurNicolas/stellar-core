// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "GlobalChecks.h"
#include "Backtrace.h"

#ifdef _WIN32
#include <Windows.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <thread>

namespace
{
void
printBacktrace()
{
    auto bt = getCurrentBacktrace();
    if (!bt.empty())
    {
        std::fprintf(stderr, "backtrace:\n");
        size_t i = 0;
        for (auto const& f : bt)
        {
            std::fprintf(stderr, "  %4d: %s\n", i++, f.c_str());
        }
        std::fflush(stderr);
    }
}
}

namespace stellar
{
static std::thread::id mainThread = std::this_thread::get_id();

void
assertThreadIsMain()
{
    dbgAssert(mainThread == std::this_thread::get_id());
}

void
dbgAbort()
{
#ifdef _WIN32
    DebugBreak();
#else
    std::abort();
#endif
}

void
printErrorAndAbort(const char* s1)
{
    std::fprintf(stderr, "%s\n", s1);
    std::fflush(stderr);
    printBacktrace();
    dbgAbort();
    std::abort();
}

void
printErrorAndAbort(const char* s1, const char* s2)
{
    std::fprintf(stderr, "%s%s\n", s1, s2);
    std::fflush(stderr);
    printBacktrace();
    dbgAbort();
    std::abort();
}

void
printAssertFailureAndAbort(const char* s1, const char* file, int line)
{
    std::fprintf(stderr, "%s at %s:%d\n", s1, file, line);
    std::fflush(stderr);
    printBacktrace();
    dbgAbort();
    std::abort();
}

void
printAssertFailureAndThrow(const char* s1, const char* file, int line)
{
    std::fprintf(stderr, "%s at %s:%d\n", s1, file, line);
    std::fflush(stderr);
    printBacktrace();
    throw std::runtime_error(s1);
}
}
