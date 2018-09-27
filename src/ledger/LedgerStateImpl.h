// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "database/Database.h"
#include "ledger/LedgerState.h"
#include "util/lrucache.hpp"

namespace stellar
{

class AbstractLedgerState::EntryIterator::AbstractImpl
{
  public:
    virtual ~AbstractImpl() { }

    virtual void advance() = 0;

    virtual bool atEnd() const = 0;

    virtual LedgerEntry const& entry() const = 0;

    virtual bool entryExists() const = 0;

    virtual LedgerKey const& key() const = 0;
};

class LedgerState::Impl
{
    class EntryIteratorImpl;

    AbstractLedgerStateParent& mParent;
    AbstractLedgerState* mChild;
    LedgerHeader mHeader;
    std::shared_ptr<LedgerStateHeader::Impl> mActiveHeader;
    std::map<LedgerKey, std::shared_ptr<LedgerEntry>> mEntry;
    std::map<LedgerKey, std::shared_ptr<EntryImplBase>> mActive;
    bool mShouldUpdateLastModified;
    bool mIsSealed;

    void checkNoChild() const;
    void checkNotSealed() const;

    std::map<AccountID, int64_t> getDeltaVotes() const;

    std::map<AccountID, int64_t>
    getTotalVotes(std::vector<InflationWinner> const& parentWinners,
                  std::map<AccountID, int64_t> const& deltaVotes,
                  int64_t minVotes) const;

    std::vector<InflationWinner>
    enumerateInflationWinners(std::map<AccountID, int64_t> const& totalVotes,
                              size_t maxWinners, int64_t minVotes) const;

    void sealAndMaybeUpdateLastModified();

  public:
    Impl(LedgerState& self, AbstractLedgerStateParent& parent,
         bool shouldUpdateLastModified);

    void addChild(AbstractLedgerState& child);

    void commit(Identifier id);

    void commitChild();

    LedgerStateEntry create(LedgerState& self, LedgerEntry const& entry);

    void deactivate(LedgerKey const& key);

    void deactivateHeader();

    void erase(LedgerKey const& key);

    std::map<LedgerKey, LedgerEntry> getAllOffers();

    std::shared_ptr<LedgerEntry>
    getBestOffer(Asset const& buying, Asset const& selling,
                 std::set<LedgerKey>&& exclude);

    LedgerEntryChanges getChanges();

    std::vector<LedgerKey> getDeadEntries();

    LedgerStateDelta getDelta();

    std::map<LedgerKey, LedgerEntry>
    getOffersByAccountAndAsset(AccountID const& account, Asset const& asset);

    AbstractLedgerState::EntryIterator getEntryIterator() const;

    LedgerHeader const& getHeader() const;

    std::vector<InflationWinner>
    getInflationWinners(size_t maxWinners, int64_t minBalance);

    std::vector<LedgerEntry> getLiveEntries();

    std::shared_ptr<LedgerEntry const>
    getNewestVersion(LedgerKey const& key) const;

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

class LedgerState::Impl::EntryIteratorImpl
    : public AbstractLedgerState::EntryIterator::AbstractImpl
{
    typedef std::map<LedgerKey, std::shared_ptr<LedgerEntry>>::const_iterator
        IteratorType;
    IteratorType mIter;
    IteratorType const mEnd;

  public:
    EntryIteratorImpl(IteratorType const& begin, IteratorType const& end)
        : mIter(begin), mEnd(end)
    {
    }

    void advance() override
    {
        ++mIter;
    }

    bool atEnd() const override
    {
        return mIter == mEnd;
    }

    LedgerEntry const& entry() const override
    {
        return *(mIter->second);
    }

    bool entryExists() const override
    {
        return (bool)(mIter->second);
    }

    LedgerKey const& key() const override
    {
        return mIter->first;
    }
};

class LedgerStateRoot::Impl
{
    typedef cache::lru_cache<std::string, std::shared_ptr<LedgerEntry const>>
        EntryCacheType;
    struct BestOffersCacheEntry
    {
        std::vector<LedgerEntry> bestOffers;
        bool allLoaded;
    };
    typedef cache::lru_cache<std::string, BestOffersCacheEntry>
        BestOffersCacheType;

    Database& mDatabase;
    LedgerHeader mHeader;
    size_t mEntryCacheSize;
    std::unique_ptr<EntryCacheType> mEntryCache;
    std::unique_ptr<BestOffersCacheType> mBestOffersCache;
    std::unique_ptr<soci::transaction> mTransaction;
    AbstractLedgerState* mChild;

    void checkNoChild() const;

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

    void storeAccount(AbstractLedgerState::EntryIterator const& iter);
    void storeData(AbstractLedgerState::EntryIterator const& iter);
    void storeOffer(AbstractLedgerState::EntryIterator const& iter);
    void storeTrustLine(AbstractLedgerState::EntryIterator const& iter);

    void storeSigners(LedgerEntry const& entry,
                      std::shared_ptr<LedgerEntry const> const& previous);

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
    Impl(Database& db, size_t entryCacheSize, size_t bestOfferCacheSize);

    void addChild(AbstractLedgerState& child);

    void commitChild();

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

    std::shared_ptr<LedgerEntry const>
    getNewestVersion(LedgerKey const& key) const;

    void rollbackChild();
};
}
