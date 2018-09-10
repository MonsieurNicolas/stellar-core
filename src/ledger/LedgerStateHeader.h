#pragma once

// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <memory>

namespace stellar
{

class AbstractLedgerState;
struct LedgerHeader;

class HeaderImpl
{
    AbstractLedgerState& mLedgerState;
    LedgerHeader& mCurrent;

  public:
    explicit HeaderImpl(AbstractLedgerState& ls, LedgerHeader& current);

    // Copy construction and copy assignment are forbidden.
    HeaderImpl(HeaderImpl const&) = delete;
    HeaderImpl& operator=(HeaderImpl const&) = delete;

    // Move construction and move assignment are forbidden.
    HeaderImpl(HeaderImpl&& other) = delete;
    HeaderImpl& operator=(HeaderImpl&& other) = delete;

    LedgerHeader& current();

    void deactivate();
};

class LedgerStateHeader
{
    std::weak_ptr<HeaderImpl> mImpl;

  public:
    explicit LedgerStateHeader(std::shared_ptr<HeaderImpl> const& impl);

    ~LedgerStateHeader();

    // Copy construction and copy assignment are forbidden.
    LedgerStateHeader(LedgerStateHeader const&) = delete;
    LedgerStateHeader& operator=(LedgerStateHeader const&) = delete;

    // Move construction and move assignment are permitted.
    LedgerStateHeader(LedgerStateHeader&& other);
    LedgerStateHeader& operator=(LedgerStateHeader&& other);

    operator bool() const;

    LedgerHeader& current();
    LedgerHeader const& current() const;

    void deactivate();
};
}
