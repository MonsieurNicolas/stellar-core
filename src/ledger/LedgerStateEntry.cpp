// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerStateEntry.h"
#include "ledger/LedgerState.h"
#include "util/types.h"
#include "util/XDROperators.h"
#include "xdr/Stellar-ledger-entries.h"

namespace stellar
{

// Implementation of LedgerStateEntry -----------------------------------------
LedgerStateEntry::LedgerStateEntry()
{
}

LedgerStateEntry::LedgerStateEntry(
    std::shared_ptr<LedgerStateEntryImpl> const& impl)
    : mImpl(impl)
{
}

LedgerStateEntryImpl::LedgerStateEntryImpl(AbstractLedgerState& ls,
                                           LedgerEntry& current)
    : mLedgerState(ls), mCurrent(current)
{
}

LedgerStateEntry::~LedgerStateEntry()
{
    auto impl = mImpl.lock();
    if (impl)
    {
        impl->deactivate();
    }
}

LedgerStateEntryImpl::~LedgerStateEntryImpl()
{
}

LedgerStateEntry::LedgerStateEntry(LedgerStateEntry&& other)
    : mImpl(std::move(other.mImpl))
{
    other.mImpl.reset();
}

// Copy-and-swap implementation ensures that *this is properly destructed (and
// deactivated) if this->mImpl != nullptr, but note that self-assignment must
// still be handled explicitly since the copy would still deactivate the entry.
LedgerStateEntry&
LedgerStateEntry::operator=(LedgerStateEntry&& other)
{
    if (this != &other)
    {
        LedgerStateEntry otherCopy(other.mImpl.lock());
        swap(otherCopy);
        other.mImpl.reset();
    }
    return *this;
}

LedgerStateEntry::operator bool() const
{
    return !mImpl.expired();
}

LedgerEntry&
LedgerStateEntry::current()
{
    return mImpl.lock()->current();
}

LedgerEntry const&
LedgerStateEntry::current() const
{
    return mImpl.lock()->current();
}

LedgerEntry&
LedgerStateEntryImpl::current()
{
    return mCurrent;
}

void
LedgerStateEntry::deactivate()
{
    mImpl.lock()->deactivate();
}

void
LedgerStateEntryImpl::deactivate()
{
    auto key = LedgerEntryKey(mCurrent);
    mLedgerState.deactivate(key);
}

void
LedgerStateEntry::erase()
{
    mImpl.lock()->erase();
}

void
LedgerStateEntryImpl::erase()
{
    auto key = LedgerEntryKey(mCurrent);
    mLedgerState.deactivate(key);
    mLedgerState.erase(key);
}

void
LedgerStateEntry::swap(LedgerStateEntry& other)
{
    mImpl.swap(other.mImpl);
}

// Implementation of ConstLedgerStateEntry ------------------------------------
ConstLedgerStateEntry::ConstLedgerStateEntry()
{
}

ConstLedgerStateEntry::ConstLedgerStateEntry(
    std::shared_ptr<ConstLedgerStateEntryImpl> const& impl)
    : mImpl(impl)
{
}

ConstLedgerStateEntryImpl::ConstLedgerStateEntryImpl(AbstractLedgerState& ls,
                                                     LedgerEntry const& current)
    : mLedgerState(ls), mCurrent(current)
{
}

ConstLedgerStateEntry::~ConstLedgerStateEntry()
{
    auto impl = mImpl.lock();
    if (impl)
    {
        impl->deactivate();
    }
}

ConstLedgerStateEntryImpl::~ConstLedgerStateEntryImpl()
{
}

ConstLedgerStateEntry::ConstLedgerStateEntry(ConstLedgerStateEntry&& other)
    : mImpl(std::move(other.mImpl))
{
    other.mImpl.reset();
}

// Copy-and-swap implementation ensures that *this is properly destructed (and
// deactivated) if this->mImpl != nullptr, but note that self-assignment must
// still be handled explicitly since the copy would still deactivate the entry.
ConstLedgerStateEntry&
ConstLedgerStateEntry::operator=(ConstLedgerStateEntry&& other)
{
    if (this != &other)
    {
        ConstLedgerStateEntry otherCopy(other.mImpl.lock());
        swap(otherCopy);
        other.mImpl.reset();
    }
    return *this;
}

ConstLedgerStateEntry::operator bool() const
{
    return !mImpl.expired();
}

LedgerEntry const&
ConstLedgerStateEntry::current() const
{
    return mImpl.lock()->current();
}

LedgerEntry const&
ConstLedgerStateEntryImpl::current() const
{
    return mCurrent;
}

void
ConstLedgerStateEntry::deactivate()
{
    mImpl.lock()->deactivate();
}

void
ConstLedgerStateEntryImpl::deactivate()
{
    auto key = LedgerEntryKey(mCurrent);
    mLedgerState.deactivate(key);
}

void
ConstLedgerStateEntry::swap(ConstLedgerStateEntry& other)
{
    mImpl.swap(other.mImpl);
}
}
