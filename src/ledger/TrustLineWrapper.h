#pragma once

// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerStateEntry.h"
#include "xdr/Stellar-ledger-entries.h"
#include <memory>

namespace stellar
{

class LedgerState;
class LedgerStateHeader;

class TrustLineWrapper
{
    class AbstractImpl;
    class IssuerImpl;
    class NonIssuerImpl;

    std::unique_ptr<AbstractImpl> mImpl;

  public:
    TrustLineWrapper();
    TrustLineWrapper(AbstractLedgerState& ls, AccountID const& accountID, Asset const& asset);
    TrustLineWrapper(LedgerStateEntry&& entry);

    operator bool() const;

    TrustLineWrapper(TrustLineWrapper const& other) = delete;
    TrustLineWrapper& operator=(TrustLineWrapper const& other) = delete;

    TrustLineWrapper(TrustLineWrapper&& other) = default;
    TrustLineWrapper& operator=(TrustLineWrapper&& other) = default;

    AccountID const& getAccountID() const;
    Asset const& getAsset() const;

    int64_t getBalance() const;
    bool addBalance(LedgerStateHeader const& header, int64_t delta);

    int64_t getBuyingLiabilities(LedgerStateHeader const& header);
    int64_t getSellingLiabilities(LedgerStateHeader const& header);

    int64_t addBuyingLiabilities(LedgerStateHeader const& header,
                                 int64_t delta);
    int64_t addSellingLiabilities(LedgerStateHeader const& header,
                                  int64_t delta);

    bool isAuthorized() const;

    int64_t getAvailableBalance(LedgerStateHeader const& header) const;

    int64_t getMaxAmountReceive(LedgerStateHeader const& header) const;

    void deactivate();
};

class TrustLineWrapper::AbstractImpl
{
  public:
    virtual ~AbstractImpl() {};

    virtual operator bool() const = 0;

    virtual AccountID const& getAccountID() const = 0;
    virtual Asset const& getAsset() const = 0;

    virtual int64_t getBalance() const = 0;
    virtual bool addBalance(LedgerStateHeader const& header, int64_t delta) = 0;

    virtual int64_t getBuyingLiabilities(LedgerStateHeader const& header) = 0;
    virtual int64_t getSellingLiabilities(LedgerStateHeader const& header) = 0;

    virtual int64_t addBuyingLiabilities(LedgerStateHeader const& header,
                                         int64_t delta) = 0;
    virtual int64_t addSellingLiabilities(LedgerStateHeader const& header,
                                          int64_t delta) = 0;

    virtual bool isAuthorized() const = 0;

    virtual int64_t getAvailableBalance(LedgerStateHeader const& header) const = 0;

    virtual int64_t getMaxAmountReceive(LedgerStateHeader const& header) const = 0;
};

class TrustLineWrapper::IssuerImpl : public TrustLineWrapper::AbstractImpl
{
    AccountID const mAccountID;
    Asset const mAsset;

  public:
    IssuerImpl(AccountID const& accountID, Asset const& asset);

    operator bool() const override;

    AccountID const& getAccountID() const override;
    Asset const& getAsset() const override;

    int64_t getBalance() const override;
    bool addBalance(LedgerStateHeader const& header, int64_t delta) override;

    int64_t getBuyingLiabilities(LedgerStateHeader const& header) override;
    int64_t getSellingLiabilities(LedgerStateHeader const& header) override;

    int64_t addBuyingLiabilities(LedgerStateHeader const& header,
                                 int64_t delta) override;
    int64_t addSellingLiabilities(LedgerStateHeader const& header,
                                  int64_t delta) override;

    bool isAuthorized() const override;

    int64_t getAvailableBalance(LedgerStateHeader const& header) const override;

    int64_t getMaxAmountReceive(LedgerStateHeader const& header) const override;
};

class TrustLineWrapper::NonIssuerImpl : public TrustLineWrapper::AbstractImpl
{
    LedgerStateEntry mEntry;

  public:
    NonIssuerImpl(LedgerStateEntry&& entry);

    operator bool() const override;

    AccountID const& getAccountID() const override;
    Asset const& getAsset() const override;

    int64_t getBalance() const override;
    bool addBalance(LedgerStateHeader const& header, int64_t delta) override;

    int64_t getBuyingLiabilities(LedgerStateHeader const& header) override;
    int64_t getSellingLiabilities(LedgerStateHeader const& header) override;

    int64_t addBuyingLiabilities(LedgerStateHeader const& header,
                                 int64_t delta) override;
    int64_t addSellingLiabilities(LedgerStateHeader const& header,
                                  int64_t delta) override;

    bool isAuthorized() const override;

    int64_t getAvailableBalance(LedgerStateHeader const& header) const override;

    int64_t getMaxAmountReceive(LedgerStateHeader const& header) const override;
};

class ConstTrustLineWrapper
{
    class AbstractImpl;
    class IssuerImpl;
    class NonIssuerImpl;

    std::unique_ptr<AbstractImpl> mImpl;

  public:
    ConstTrustLineWrapper();
    ConstTrustLineWrapper(AbstractLedgerState& ls, AccountID const& accountID, Asset const& asset);
    ConstTrustLineWrapper(ConstLedgerStateEntry&& entry);

    operator bool() const;

    ConstTrustLineWrapper(ConstTrustLineWrapper const& other) = delete;
    ConstTrustLineWrapper& operator=(ConstTrustLineWrapper const& other) = delete;

    ConstTrustLineWrapper(ConstTrustLineWrapper&& other) = default;
    ConstTrustLineWrapper& operator=(ConstTrustLineWrapper&& other) = default;

    int64_t getBalance() const;

    bool isAuthorized() const;

    int64_t getAvailableBalance(LedgerStateHeader const& header) const;

    int64_t getMaxAmountReceive(LedgerStateHeader const& header) const;

    void deactivate();
};

class ConstTrustLineWrapper::AbstractImpl
{
  public:
    virtual ~AbstractImpl() {};

    virtual operator bool() const = 0;

    virtual int64_t getBalance() const = 0;

    virtual bool isAuthorized() const = 0;

    virtual int64_t getAvailableBalance(LedgerStateHeader const& header) const = 0;

    virtual int64_t getMaxAmountReceive(LedgerStateHeader const& header) const = 0;
};

class ConstTrustLineWrapper::NonIssuerImpl : public ConstTrustLineWrapper::AbstractImpl
{
    ConstLedgerStateEntry mEntry;

  public:
    NonIssuerImpl(ConstLedgerStateEntry&& entry);

    operator bool() const override;

    int64_t getBalance() const override;

    bool isAuthorized() const override;

    int64_t getAvailableBalance(LedgerStateHeader const& header) const override;

    int64_t getMaxAmountReceive(LedgerStateHeader const& header) const override;
};

class ConstTrustLineWrapper::IssuerImpl : public ConstTrustLineWrapper::AbstractImpl
{
  public:
    operator bool() const override;

    int64_t getBalance() const override;

    bool isAuthorized() const override;

    int64_t getAvailableBalance(LedgerStateHeader const& header) const override;

    int64_t getMaxAmountReceive(LedgerStateHeader const& header) const override;
};
}