// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "util/Logging.h"
#include <chrono>
#include <util/format.h>

namespace stellar
{
class LogSlowExecution
{
  public:
    enum class Mode
    {
        AUTOMATIC_RAII,
        MANUAL // In this mode, it is the caller's responsibility to check
               // elapsed time
    };

    LogSlowExecution(
        std::string eventName, Mode mode = Mode::AUTOMATIC_RAII,
        std::string message = "took",
        std::chrono::milliseconds threshold = std::chrono::seconds(1),
        el::Level logLevel = el::Level::Info)
        : mStart(std::chrono::system_clock::now())
        , mName(std::move(eventName))
        , mMode(mode)
        , mMessage(std::move(message))
        , mThreshold(threshold)
        , mLogLevel(logLevel){};

    ~LogSlowExecution()
    {
        if (mMode == Mode::AUTOMATIC_RAII)
        {
            checkElapsedTime();
        }
    }

    std::chrono::milliseconds
    checkElapsedTime() const
    {
        auto finish = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            finish - mStart);
        auto tooSlow = elapsed > mThreshold;

        if (tooSlow && mLogLevel >= Logging::getLogLevel("Perf"))
        {
            std::string m =
                fmt::format("'{}' {} {} s", mName, mMessage,
                            static_cast<float>(elapsed.count()) / 1000);
            CLOG(INFO, "Perf") << m;
        }
        return elapsed;
    }

  private:
    std::chrono::system_clock::time_point mStart;
    std::string mName;
    Mode mMode;
    std::string mMessage;
    std::chrono::milliseconds mThreshold;
    el::Level mLogLevel;
};
}
