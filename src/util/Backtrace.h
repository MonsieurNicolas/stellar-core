#pragma once

// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <string>
#include <vector>

// Returns a vector of function names (first being innermost) for the demangled
// names of all functions in the function-call backtrace of the current caller.
std::vector<std::string> getCurrentBacktrace();
