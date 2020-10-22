// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/Backtrace.h"

#ifdef _WIN32

// Backtrace-generation is not suported on windows.
std::vector<std::string>
getCurrentBacktrace()
{
    std::vector<std::string> res;
    return res;
}

#else

#define UNW_LOCAL_ONLY
#include <cxxabi.h>
#include <libunwind.h>

std::vector<std::string>
getCurrentBacktrace()
{
    std::vector<std::string> res;

    unw_context_t ctxt;
    if (unw_getcontext(&ctxt) != 0)
    {
        return res;
    }

    unw_cursor_t curs;
    if (unw_init_local(&curs, &ctxt) != 0)
    {
        return res;
    }

    char buf[1024];
    unw_word_t off;
    while (unw_step(&curs) > 0)
    {
        if (unw_get_proc_name(&curs, buf, sizeof(buf), &off) != 0)
        {
            continue;
        }
        int status = 0;
        char* demangled = abi::__cxa_demangle(buf, nullptr, nullptr, &status);
        if (status == 0)
        {
            res.emplace_back(demangled);
            free(demangled);
        }
        else
        {
            res.emplace_back(buf);
        }
    }
    return res;
}

#endif
