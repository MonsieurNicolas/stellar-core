// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerState.h"
#include "ledger/LedgerStateHeader.h"
#include "ledger/TrustLineWrapper.h"
#include "transactions/TransactionUtils.h"
#include "util/types.h"
#include "util/XDROperators.h"

namespace stellar
{

// Implementation of TrustLineWrapper -----------------------------------------
TrustLineWrapper::TrustLineWrapper()
{
}

TrustLineWrapper::TrustLineWrapper(AbstractLedgerState& ls,
                                   AccountID const& accountID,
                                   Asset const& asset)
{
    if (asset.type() == ASSET_TYPE_NATIVE)
    {
        throw std::runtime_error("trustline for native asset");
    }

    if (!(getIssuer(asset) == accountID))
    {
        LedgerKey key(TRUSTLINE);
        key.trustLine().accountID = accountID;
        key.trustLine().asset = asset;
        auto entry = ls.load(key);
        if (entry)
        {
            mImpl = std::make_unique<NonIssuerImpl>(std::move(entry));
        }
    }
    else
    {
        mImpl = std::make_unique<IssuerImpl>(accountID, asset);
    }
}

TrustLineWrapper::TrustLineWrapper(LedgerStateEntry&& entry)
{
    if (entry)
    {
        mImpl = std::make_unique<NonIssuerImpl>(std::move(entry));
    }
}

TrustLineWrapper::operator bool() const
{
    return (bool)mImpl && (bool)(*mImpl);
}

AccountID const&
TrustLineWrapper::getAccountID() const
{
    return mImpl->getAccountID();
}

Asset const&
TrustLineWrapper::getAsset() const
{
    return mImpl->getAsset();
}

int64_t
TrustLineWrapper::getBalance() const
{
    return mImpl->getBalance();
}

bool
TrustLineWrapper::addBalance(LedgerStateHeader const& header,
                             int64_t delta)
{
    return mImpl->addBalance(header, delta);
}


int64_t
TrustLineWrapper::getBuyingLiabilities(LedgerStateHeader const& header)
{
    return mImpl->getBuyingLiabilities(header);
}

int64_t
TrustLineWrapper::getSellingLiabilities(LedgerStateHeader const& header)
{
    return mImpl->getSellingLiabilities(header);
}

int64_t
TrustLineWrapper::addBuyingLiabilities(LedgerStateHeader const& header,
                                       int64_t delta)
{
    return mImpl->addBuyingLiabilities(header, delta);
}

int64_t
TrustLineWrapper::addSellingLiabilities(LedgerStateHeader const& header,
                                        int64_t delta)
{
    return mImpl->addSellingLiabilities(header, delta);
}

bool
TrustLineWrapper::isAuthorized() const
{
    return mImpl->isAuthorized();
}

int64_t
TrustLineWrapper::getAvailableBalance(LedgerStateHeader const& header) const
{
    return mImpl->getAvailableBalance(header);
}

int64_t
TrustLineWrapper::getMaxAmountReceive(LedgerStateHeader const& header) const
{
    return mImpl->getMaxAmountReceive(header);
}

void
TrustLineWrapper::deactivate()
{
    mImpl.reset();
}

// Implementation of TrustLineWrapper::NonIssuerImpl --------------------------
TrustLineWrapper::NonIssuerImpl::NonIssuerImpl(LedgerStateEntry&& entry)
    : mEntry(std::move(entry))
{
}

TrustLineWrapper::NonIssuerImpl::operator bool() const
{
    return (bool)mEntry;
}

AccountID const&
TrustLineWrapper::NonIssuerImpl::getAccountID() const
{
    return mEntry.current().data.trustLine().accountID;
}

Asset const&
TrustLineWrapper::NonIssuerImpl::getAsset() const
{
    return mEntry.current().data.trustLine().asset;
}

int64_t
TrustLineWrapper::NonIssuerImpl::getBalance() const
{
    return mEntry.current().data.trustLine().balance;
}

bool
TrustLineWrapper::NonIssuerImpl::addBalance(LedgerStateHeader const& header,
                             int64_t delta)
{
    return stellar::addBalance(header, mEntry, delta);
}


int64_t
TrustLineWrapper::NonIssuerImpl::getBuyingLiabilities(LedgerStateHeader const& header)
{
    return stellar::getBuyingLiabilities(header, mEntry);
}

int64_t
TrustLineWrapper::NonIssuerImpl::getSellingLiabilities(LedgerStateHeader const& header)
{
    return stellar::getSellingLiabilities(header, mEntry);
}

int64_t
TrustLineWrapper::NonIssuerImpl::addBuyingLiabilities(LedgerStateHeader const& header,
                                       int64_t delta)
{
    return stellar::addBuyingLiabilities(header, mEntry, delta);
}

int64_t
TrustLineWrapper::NonIssuerImpl::addSellingLiabilities(LedgerStateHeader const& header,
                                        int64_t delta)
{
    return stellar::addSellingLiabilities(header, mEntry, delta);
}

bool
TrustLineWrapper::NonIssuerImpl::isAuthorized() const
{
    return stellar::isAuthorized(mEntry);
}

int64_t
TrustLineWrapper::NonIssuerImpl::getAvailableBalance(LedgerStateHeader const& header) const
{
    return stellar::getAvailableBalance(header, mEntry);
}

int64_t
TrustLineWrapper::NonIssuerImpl::getMaxAmountReceive(LedgerStateHeader const& header) const
{
    return stellar::getMaxAmountReceive(header, mEntry);
}

// Implementation of TrustLineWrapper::IssuerImpl -----------------------------
TrustLineWrapper::IssuerImpl::IssuerImpl(AccountID const& accountID, Asset const& asset)
    : mAccountID(accountID), mAsset(asset)
{
}

TrustLineWrapper::IssuerImpl::operator bool() const
{
    return true;
}

AccountID const&
TrustLineWrapper::IssuerImpl::getAccountID() const
{
    return mAccountID;
}

Asset const&
TrustLineWrapper::IssuerImpl::getAsset() const
{
    return mAsset;
}

int64_t
TrustLineWrapper::IssuerImpl::getBalance() const
{
    return INT64_MAX;
}

bool
TrustLineWrapper::IssuerImpl::addBalance(LedgerStateHeader const& header,
                                         int64_t delta)
{
    return true;
}

int64_t
TrustLineWrapper::IssuerImpl::getBuyingLiabilities(LedgerStateHeader const& header)
{
    return 0;
}

int64_t
TrustLineWrapper::IssuerImpl::getSellingLiabilities(LedgerStateHeader const& header)
{
    return 0;
}

int64_t
TrustLineWrapper::IssuerImpl::addBuyingLiabilities(LedgerStateHeader const& header,
                                                   int64_t delta)
{
    return true;
}

int64_t
TrustLineWrapper::IssuerImpl::addSellingLiabilities(LedgerStateHeader const& header,
                                                    int64_t delta)
{
    return true;
}

bool
TrustLineWrapper::IssuerImpl::isAuthorized() const
{
    return true;
}

int64_t
TrustLineWrapper::IssuerImpl::getAvailableBalance(LedgerStateHeader const& header) const
{
    return INT64_MAX;
}

int64_t
TrustLineWrapper::IssuerImpl::getMaxAmountReceive(LedgerStateHeader const& header) const
{
    return INT64_MAX;
}

// Implementation of ConstTrustLineWrapper ------------------------------------
ConstTrustLineWrapper::ConstTrustLineWrapper()
{
}

ConstTrustLineWrapper::ConstTrustLineWrapper(AbstractLedgerState& ls,
                                             AccountID const& accountID,
                                             Asset const& asset)
{
    if (!(getIssuer(asset) == accountID))
    {
        LedgerKey key(TRUSTLINE);
        key.trustLine().accountID = accountID;
        key.trustLine().asset = asset;
        auto entry = ls.loadWithoutRecord(key);
        if (entry)
        {
            mImpl = std::make_unique<NonIssuerImpl>(std::move(entry));
        }
    }
    else
    {
        mImpl = std::make_unique<IssuerImpl>();
    }
}

ConstTrustLineWrapper::ConstTrustLineWrapper(ConstLedgerStateEntry&& entry)
{
    if (entry)
    {
        mImpl = std::make_unique<NonIssuerImpl>(std::move(entry));
    }
}

ConstTrustLineWrapper::operator bool() const
{
    return (bool)mImpl && (bool)(*mImpl);
}

int64_t
ConstTrustLineWrapper::getBalance() const
{
    return mImpl->getBalance();
}

bool
ConstTrustLineWrapper::isAuthorized() const
{
    return mImpl->isAuthorized();
}

int64_t
ConstTrustLineWrapper::getAvailableBalance(LedgerStateHeader const& header) const
{
    return mImpl->getAvailableBalance(header);
}

int64_t
ConstTrustLineWrapper::getMaxAmountReceive(LedgerStateHeader const& header) const
{
    return mImpl->getMaxAmountReceive(header);
}

// Implementation of ConstTrustLineWrapper::NonIssuerImpl ---------------------
ConstTrustLineWrapper::NonIssuerImpl::NonIssuerImpl(ConstLedgerStateEntry&& entry)
    : mEntry(std::move(entry))
{
}

ConstTrustLineWrapper::NonIssuerImpl::operator bool() const
{
    return (bool)mEntry;
}

int64_t
ConstTrustLineWrapper::NonIssuerImpl::getBalance() const
{
    return mEntry.current().data.trustLine().balance;
}

bool
ConstTrustLineWrapper::NonIssuerImpl::isAuthorized() const
{
    return stellar::isAuthorized(mEntry);
}

int64_t
ConstTrustLineWrapper::NonIssuerImpl::getAvailableBalance(LedgerStateHeader const& header) const
{
    return stellar::getAvailableBalance(header, mEntry);
}

int64_t
ConstTrustLineWrapper::NonIssuerImpl::getMaxAmountReceive(LedgerStateHeader const& header) const
{
    return stellar::getMaxAmountReceive(header, mEntry);
}

// Implementation of ConstTrustLineWrapper::IssuerImpl ------------------------
ConstTrustLineWrapper::IssuerImpl::operator bool() const
{
    return true;
}

int64_t
ConstTrustLineWrapper::IssuerImpl::getBalance() const
{
    return INT64_MAX;
}

bool
ConstTrustLineWrapper::IssuerImpl::isAuthorized() const
{
    return true;
}

int64_t
ConstTrustLineWrapper::IssuerImpl::getAvailableBalance(LedgerStateHeader const& header) const
{
    return INT64_MAX;
}

int64_t
ConstTrustLineWrapper::IssuerImpl::getMaxAmountReceive(LedgerStateHeader const& header) const
{
    return INT64_MAX;
}
}
