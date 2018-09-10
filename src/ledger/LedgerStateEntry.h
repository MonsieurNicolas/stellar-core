#pragma once

// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "xdr/Stellar-ledger-entries.h"
#include <memory>

namespace stellar
{

class AbstractLedgerState;

class EntryImplBase
{
  public:
    virtual ~EntryImplBase()
    {
    }
};

class LedgerStateEntryImpl : public EntryImplBase
{
    AbstractLedgerState& mLedgerState;
    LedgerEntry& mCurrent;

  public:
    explicit LedgerStateEntryImpl(AbstractLedgerState& ls,
                                  LedgerEntry& current);

    ~LedgerStateEntryImpl() override;

    // Copy construction and copy assignment are forbidden.
    LedgerStateEntryImpl(LedgerStateEntryImpl const&) = delete;
    LedgerStateEntryImpl& operator=(LedgerStateEntryImpl const&) = delete;

    // Move construction and move assignment are forbidden.
    LedgerStateEntryImpl(LedgerStateEntryImpl&& other) = delete;
    LedgerStateEntryImpl& operator=(LedgerStateEntryImpl&& other) = delete;

    LedgerEntry& current();

    void deactivate();

    void erase();
};

class LedgerStateEntry
{
    std::weak_ptr<LedgerStateEntryImpl> mImpl;

  public:
    LedgerStateEntry();
    explicit LedgerStateEntry(
        std::shared_ptr<LedgerStateEntryImpl> const& impl);

    ~LedgerStateEntry();

    // Copy construction and copy assignment are forbidden.
    LedgerStateEntry(LedgerStateEntry const&) = delete;
    LedgerStateEntry& operator=(LedgerStateEntry const&);

    // Move construction and move assignment are permitted.
    LedgerStateEntry(LedgerStateEntry&& other);
    LedgerStateEntry& operator=(LedgerStateEntry&& other);

    operator bool() const;

    LedgerEntry& current();
    LedgerEntry const& current() const;

    void deactivate();

    void erase();

    void swap(LedgerStateEntry& other);
};

class ConstLedgerStateEntryImpl : public EntryImplBase
{
    AbstractLedgerState& mLedgerState;
    LedgerEntry const mCurrent;

  public:
    explicit ConstLedgerStateEntryImpl(AbstractLedgerState& ls,
                                       LedgerEntry const& current);

    ~ConstLedgerStateEntryImpl() override;

    // Copy construction and copy assignment are forbidden.
    ConstLedgerStateEntryImpl(ConstLedgerStateEntryImpl const&) = delete;
    ConstLedgerStateEntryImpl&
    operator=(ConstLedgerStateEntryImpl const&) = delete;

    // Move construction and move assignment are forbidden.
    ConstLedgerStateEntryImpl(ConstLedgerStateEntryImpl&& other) = delete;
    ConstLedgerStateEntryImpl&
    operator=(ConstLedgerStateEntryImpl&& other) = delete;

    LedgerEntry const& current() const;

    void deactivate();
};

class ConstLedgerStateEntry
{
    std::weak_ptr<ConstLedgerStateEntryImpl> mImpl;

  public:
    ConstLedgerStateEntry();
    explicit ConstLedgerStateEntry(
        std::shared_ptr<ConstLedgerStateEntryImpl> const& impl);

    ~ConstLedgerStateEntry();

    // Copy construction and copy assignment are forbidden.
    ConstLedgerStateEntry(ConstLedgerStateEntry const&) = delete;
    ConstLedgerStateEntry& operator=(ConstLedgerStateEntry const&) = delete;

    // Move construction and move assignment are permitted.
    ConstLedgerStateEntry(ConstLedgerStateEntry&& other);
    ConstLedgerStateEntry& operator=(ConstLedgerStateEntry&& other);

    operator bool() const;

    LedgerEntry const& current() const;

    void deactivate();

    void swap(ConstLedgerStateEntry& other);
};
}
