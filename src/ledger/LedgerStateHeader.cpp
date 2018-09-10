// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerStateHeader.h"
#include "ledger/LedgerState.h"

namespace stellar
{

LedgerStateHeader::LedgerStateHeader(std::shared_ptr<HeaderImpl> const& impl)
    : mImpl(impl)
{
}

HeaderImpl::HeaderImpl(AbstractLedgerState& ls, LedgerHeader& current)
    : mLedgerState(ls), mCurrent(current)
{
}

LedgerStateHeader::~LedgerStateHeader()
{
    auto impl = mImpl.lock();
    if (impl)
    {
        impl->deactivate();
    }
}

LedgerStateHeader::LedgerStateHeader(LedgerStateHeader&& other)
    : mImpl(std::move(other.mImpl))
{
    other.mImpl.reset();
}

LedgerStateHeader&
LedgerStateHeader::operator=(LedgerStateHeader&& other)
{
    if (&other == this)
    {
        return *this;
    }

    auto impl = mImpl.lock();
    if (impl)
    {
        impl->deactivate();
    }

    mImpl = std::move(other.mImpl);
    other.mImpl.reset();
    return *this;
}

LedgerStateHeader::operator bool() const
{
    return !mImpl.expired();
}

LedgerHeader&
LedgerStateHeader::current()
{
    return mImpl.lock()->current();
}

LedgerHeader const&
LedgerStateHeader::current() const
{
    return mImpl.lock()->current();
}

LedgerHeader&
HeaderImpl::current()
{
    return mCurrent;
}

void
LedgerStateHeader::deactivate()
{
    mImpl.lock()->deactivate();
}

void
HeaderImpl::deactivate()
{
    mLedgerState.deactivateHeader();
}
}
