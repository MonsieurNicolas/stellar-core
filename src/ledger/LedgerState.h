#pragma once

// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "database/Database.h"
#include "util/lrucache.hpp"
#include "xdr/Stellar-ledger.h"
#include <map>
#include <memory>
#include <queue>
#include <set>

namespace stellar
{

class ConstLedgerStateEntry;
class LedgerStateEntry;
class EntryImplBase;
class LedgerStateHeader;
class HeaderImpl;
struct InflationVotes;
struct LedgerEntry;
struct LedgerKey;
class LedgerRange;

bool isBetterOffer(LedgerEntry const& lhsEntry, LedgerEntry const& rhsEntry);

class AbstractLedgerState;

struct InflationWinner
{
    AccountID accountID;
    int64_t votes;
};

struct LedgerStateDelta
{
    struct EntryDelta
    {
        std::shared_ptr<LedgerEntry const> current;
        std::shared_ptr<LedgerEntry const> previous;
    };

    struct HeaderDelta
    {
        LedgerHeader current;
        LedgerHeader previous;
    };

    std::map<LedgerKey, EntryDelta> entry;
    HeaderDelta header;
};

class AbstractLedgerStateParent
{
  public:
    class Identifier
    {
        friend class AbstractLedgerStateParent;
        Identifier(){};
    };

    virtual ~AbstractLedgerStateParent() { }

    virtual void addChild(AbstractLedgerState& child) = 0;

    virtual void commitChild(Identifier id) = 0;

    virtual std::map<LedgerKey, LedgerEntry> getAllOffers() = 0;

    virtual std::shared_ptr<LedgerEntry>
    getBestOffer(Asset const& buying, Asset const& selling,
                 std::set<LedgerKey>&& exclude) = 0;

    virtual std::map<LedgerKey, LedgerEntry>
    getOffersByAccountAndAsset(AccountID const& account, Asset const& asset) = 0;

    virtual LedgerHeader const& getHeader() const = 0;

    virtual std::vector<InflationWinner>
    getInflationWinners(size_t maxWinners, int64_t minBalance) = 0;

    struct EntryVersion
    {
        AbstractLedgerStateParent const* ledgerState;
        std::shared_ptr<LedgerEntry const> entry;
    };
    virtual EntryVersion getNewestVersion(LedgerKey const& key) const = 0;

    virtual void rollbackChild(Identifier id) = 0;

  protected:
    Identifier getIdentifier() const;
};

class AbstractLedgerState : public AbstractLedgerStateParent
{
    friend class LedgerStateEntryImpl;
    friend class ConstLedgerStateEntryImpl;
    virtual void deactivate(LedgerKey const& key) = 0;

    friend class HeaderImpl;
    virtual void deactivateHeader() = 0;

  public:
    virtual ~AbstractLedgerState() { }

    virtual void commit() = 0;

    virtual LedgerStateEntry create(LedgerEntry const& entry) = 0;

    virtual void erase(LedgerKey const& key) = 0;

    virtual LedgerEntryChanges getChanges() = 0;

    virtual std::vector<LedgerKey> getDeadEntries() = 0;

    virtual LedgerStateDelta getDelta() = 0;

    virtual std::map<LedgerKey, std::shared_ptr<LedgerEntry const>>
    getEntries() const = 0;

    virtual std::vector<LedgerEntry> getLiveEntries() = 0;

    virtual LedgerStateEntry load(LedgerKey const& key) = 0;

    virtual std::map<AccountID, std::vector<LedgerStateEntry>>
    loadAllOffers() = 0;

    virtual LedgerStateEntry loadBestOffer(Asset const& buying,
                                           Asset const& selling) = 0;

    virtual LedgerStateHeader loadHeader() = 0;

    virtual std::vector<LedgerStateEntry>
    loadOffersByAccountAndAsset(AccountID const& accountID,
                                Asset const& asset) = 0;

    virtual ConstLedgerStateEntry loadWithoutRecord(LedgerKey const& key) = 0;

    virtual void rollback() = 0;

    virtual void unsealHeader(std::function<void(LedgerHeader&)> f) = 0;
};

class LedgerState : public AbstractLedgerState
{
    class Impl;
    std::unique_ptr<Impl> mImpl;

    void deactivate(LedgerKey const& key) override;

    void deactivateHeader() override;

  public:
    explicit LedgerState(AbstractLedgerStateParent& parent,
                         bool shouldUpdateLastModified = true);
    explicit LedgerState(LedgerState& parent, bool shouldUpdateLastModified = true);

    virtual ~LedgerState();

    void addChild(AbstractLedgerState& child) override;

    void commit() override;

    void commitChild(Identifier id) override;

    LedgerStateEntry create(LedgerEntry const& entry) override;

    void erase(LedgerKey const& key) override;

    std::map<LedgerKey, LedgerEntry> getAllOffers() override;

    std::shared_ptr<LedgerEntry>
    getBestOffer(Asset const& buying, Asset const& selling,
                 std::set<LedgerKey>&& exclude) override;

    LedgerEntryChanges getChanges() override;

    std::vector<LedgerKey> getDeadEntries() override;

    LedgerStateDelta getDelta() override;

    std::map<LedgerKey, LedgerEntry>
    getOffersByAccountAndAsset(AccountID const& account, Asset const& asset) override;

    std::map<LedgerKey, std::shared_ptr<LedgerEntry const>>
    getEntries() const override;

    LedgerHeader const& getHeader() const override;

    std::vector<InflationWinner>
    getInflationWinners(size_t maxWinners, int64_t minBalance) override;

    std::vector<LedgerEntry> getLiveEntries() override;

    EntryVersion getNewestVersion(LedgerKey const& key) const override;

    LedgerStateEntry load(LedgerKey const& key) override;

    std::map<AccountID, std::vector<LedgerStateEntry>>
    loadAllOffers() override;

    LedgerStateEntry loadBestOffer(Asset const& buying,
                                   Asset const& selling) override;

    LedgerStateHeader loadHeader() override;

    std::vector<LedgerStateEntry>
    loadOffersByAccountAndAsset(AccountID const& accountID,
                                Asset const& asset) override;

    ConstLedgerStateEntry loadWithoutRecord(LedgerKey const& key) override;

    void rollback() override;

    void rollbackChild(Identifier id) override;

    void unsealHeader(std::function<void(LedgerHeader&)> f) override;
};

class LedgerState::Impl
{
    AbstractLedgerStateParent& mParent;
    AbstractLedgerState* mChild;
    LedgerHeader mHeader;
    std::shared_ptr<HeaderImpl> mActiveHeader;
    std::map<LedgerKey, std::shared_ptr<LedgerEntry>> mEntry;
    std::map<LedgerKey, std::shared_ptr<EntryImplBase>> mActive;
    bool mShouldUpdateLastModified;
    bool mIsSealed;

    void checkNoChild() const;
    void checkNotSealed() const;

    void updateLastModified();

  public:
    Impl(LedgerState& self, AbstractLedgerStateParent& parent,
         bool shouldUpdateLastModified);

    void addChild(AbstractLedgerState& child);

    void commit(Identifier id);

    void commitChild(LedgerState& self);

    LedgerStateEntry create(LedgerState& self, LedgerEntry const& entry);

    void deactivate(LedgerKey const& key);

    void deactivateHeader();

    void erase(LedgerState const& self, LedgerKey const& key);

    std::map<LedgerKey, LedgerEntry> getAllOffers();

    std::shared_ptr<LedgerEntry>
    getBestOffer(Asset const& buying, Asset const& selling,
                 std::set<LedgerKey>&& exclude);

    LedgerEntryChanges getChanges();

    std::vector<LedgerKey> getDeadEntries();

    LedgerStateDelta getDelta();

    std::map<LedgerKey, LedgerEntry>
    getOffersByAccountAndAsset(AccountID const& account, Asset const& asset);

    std::map<LedgerKey, std::shared_ptr<LedgerEntry const>> getEntries() const;

    LedgerHeader const& getHeader(LedgerState const& self) const;

    std::vector<InflationWinner>
    getInflationWinners(size_t maxWinners, int64_t minBalance);

    std::vector<LedgerEntry> getLiveEntries();

    EntryVersion
    getNewestVersion(LedgerState const& self, LedgerKey const& key) const;

    LedgerStateEntry load(LedgerState& self, LedgerKey const& key);

    std::map<AccountID, std::vector<LedgerStateEntry>> loadAllOffers(LedgerState& self);

    LedgerStateEntry loadBestOffer(LedgerState& self, Asset const& buying,
                                   Asset const& selling);

    LedgerStateHeader loadHeader(LedgerState& self);

    std::vector<LedgerStateEntry>
    loadOffersByAccountAndAsset(LedgerState& self, AccountID const& accountID,
                                Asset const& asset);

    ConstLedgerStateEntry loadWithoutRecord(LedgerState& self,
                                            LedgerKey const& key);

    void rollback(Identifier id);

    void rollbackChild();

    void unsealHeader(LedgerState& self, std::function<void(LedgerHeader&)> f);
};

class LedgerStateRoot : public AbstractLedgerStateParent
{
    class Impl;
    std::unique_ptr<Impl> mImpl;

  public:
    explicit LedgerStateRoot(Database& db, size_t cacheSize = 4096,
                             size_t bestOfferCacheSize = 64);

    virtual ~LedgerStateRoot() { }

    void addChild(AbstractLedgerState& child) override;

    void commitChild(Identifier id) override;

    uint64_t countObjects(LedgerEntryType let) const;
    uint64_t countObjects(LedgerEntryType let, LedgerRange const& ledgers) const;

    void deleteObjectsModifiedOnOrAfterLedger(uint32_t ledger) const;

    void dropAccounts();
    void dropData();
    void dropOffers();
    void dropTrustLines();

    std::map<LedgerKey, LedgerEntry> getAllOffers() override;

    std::shared_ptr<LedgerEntry>
    getBestOffer(Asset const& buying, Asset const& selling,
                 std::set<LedgerKey>&& exclude) override;

    std::map<LedgerKey, LedgerEntry>
    getOffersByAccountAndAsset(AccountID const& account, Asset const& asset) override;

    LedgerHeader const& getHeader() const override;

    std::vector<InflationWinner>
    getInflationWinners(size_t maxWinners, int64_t minBalance) override;

    EntryVersion getNewestVersion(LedgerKey const& key) const override;

    void rollbackChild(Identifier id) override;
};

class LedgerStateRoot::Impl
{
    typedef cache::lru_cache<std::string, std::shared_ptr<LedgerEntry const>>
        CacheType;
    typedef cache::lru_cache<std::string, std::pair<std::vector<LedgerEntry>, bool>>
        BestOffersCacheType;

    Database& mDatabase;
    LedgerHeader mHeader;
    size_t mCacheSize;
    std::unique_ptr<CacheType> mCache;
    std::unique_ptr<BestOffersCacheType> mBestOffersCache;
    std::unique_ptr<soci::transaction> mTransaction;
    AbstractLedgerState* mChild;

    std::shared_ptr<LedgerEntry const> loadAccount(LedgerKey const& key) const;
    std::shared_ptr<LedgerEntry const> loadData(LedgerKey const& key) const;
    std::shared_ptr<LedgerEntry const> loadOffer(LedgerKey const& key) const;
    std::vector<LedgerEntry> loadAllOffers() const;
    std::vector<LedgerEntry> loadBestOffers(Asset const& buying,
                                            Asset const& selling,
                                            size_t numOffers,
                                            size_t offset) const;
    std::vector<LedgerEntry> loadOffersByAccountAndAsset(AccountID const& accountID,
                                                         Asset const& asset) const;
    std::vector<LedgerEntry> loadOffers(StatementContext& prep) const;
    std::vector<Signer> loadSigners(LedgerKey const& key) const;
    std::vector<InflationWinner> loadInflationWinners(size_t maxWinners, int64_t minBalance) const;
    std::shared_ptr<LedgerEntry const>
    loadTrustLine(LedgerKey const& key) const;

    void storeAccount(LedgerStateRoot const& self, LedgerKey const& key,
                      std::shared_ptr<LedgerEntry const> const& entry);
    void storeData(LedgerStateRoot const& self, LedgerKey const& key,
                   std::shared_ptr<LedgerEntry const> const& entry);
    void storeOffer(LedgerStateRoot const& self, LedgerKey const& key,
                    std::shared_ptr<LedgerEntry const> const& entry);
    void storeSigners(LedgerEntry const& entry,
                      std::shared_ptr<LedgerEntry const> const& previous);
    void storeTrustLine(LedgerStateRoot const& self, LedgerKey const& key,
                        std::shared_ptr<LedgerEntry const> const& entry);

    void deleteAccount(LedgerKey const& key);
    void deleteData(LedgerKey const& key);
    void deleteOffer(LedgerKey const& key);
    void deleteTrustLine(LedgerKey const& key);

    void insertOrUpdateAccount(LedgerEntry const& entry, bool isInsert);
    void insertOrUpdateData(LedgerEntry const& entry, bool isInsert);
    void insertOrUpdateOffer(LedgerEntry const& entry, bool isInsert);
    void insertOrUpdateTrustLine(LedgerEntry const& entry, bool isInsert);

    static std::string tableFromLedgerEntryType(LedgerEntryType let);

  public:
    Impl(Database& db, size_t cacheSize, size_t bestOfferCacheSize);

    void addChild(AbstractLedgerState& child);

    void commitChild(LedgerStateRoot const& self);

    uint64_t countObjects(LedgerEntryType let) const;
    uint64_t countObjects(LedgerEntryType let, LedgerRange const& ledgers) const;

    void deleteObjectsModifiedOnOrAfterLedger(uint32_t ledger) const;

    void dropAccounts();
    void dropData();
    void dropOffers();
    void dropTrustLines();

    std::map<LedgerKey, LedgerEntry> getAllOffers();

    std::shared_ptr<LedgerEntry>
    getBestOffer(Asset const& buying, Asset const& selling,
                 std::set<LedgerKey>&& exclude);

    std::map<LedgerKey, LedgerEntry>
    getOffersByAccountAndAsset(AccountID const& account, Asset const& asset);

    LedgerHeader const& getHeader() const;

    std::vector<InflationWinner>
    getInflationWinners(size_t maxWinners, int64_t minBalance);

    EntryVersion
    getNewestVersion(LedgerStateRoot const& self, LedgerKey const& key) const;

    void rollbackChild();
};
}
