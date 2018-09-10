// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerState.h"
#include "crypto/Hex.h"
#include "crypto/KeyUtils.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "ledger/LedgerRange.h"
#include "ledger/LedgerStateEntry.h"
#include "ledger/LedgerStateHeader.h"
#include "util/types.h"
#include "util/XDROperators.h"
#include "xdr/Stellar-ledger-entries.h"
#include "xdrpp/marshal.h"
#include <soci.h>

namespace stellar
{

bool
isBetterOffer(LedgerEntry const& lhsEntry, LedgerEntry const& rhsEntry)
{
    auto const& lhs = lhsEntry.data.offer();
    auto const& rhs = rhsEntry.data.offer();

    assert(lhs.buying == rhs.buying);
    assert(lhs.selling == rhs.selling);

    double lhsPrice = double(lhs.price.n) / double(lhs.price.d);
    double rhsPrice = double(rhs.price.n) / double(rhs.price.d);
    if (lhsPrice < rhsPrice)
    {
        return true;
    }
    else if (lhsPrice == rhsPrice)
    {
        return lhs.offerID < rhs.offerID;
    }
    else
    {
        return false;
    }
}

// Implementation of AbstractLedgerState --------------------------------------
AbstractLedgerStateParent::Identifier
AbstractLedgerStateParent::getIdentifier() const
{
    return Identifier();
}

// Implementation of LedgerState ----------------------------------------------
LedgerState::LedgerState(AbstractLedgerStateParent& parent,
                         bool shouldUpdateLastModified)
    : mImpl(std::make_unique<Impl>(*this, parent, shouldUpdateLastModified))
{
}

LedgerState::LedgerState(LedgerState& parent, bool shouldUpdateLastModified)
    : LedgerState((AbstractLedgerStateParent&)parent, shouldUpdateLastModified)
{
}

LedgerState::Impl::Impl(LedgerState& self, AbstractLedgerStateParent& parent,
                        bool shouldUpdateLastModified)
    : mParent(parent), mChild(nullptr), mHeader(mParent.getHeader())
    , mShouldUpdateLastModified(shouldUpdateLastModified)
    , mIsSealed(false)
{
    mParent.addChild(self);
}

LedgerState::~LedgerState()
{
    if (mImpl)
    {
        rollback();
    }
}

void
LedgerState::addChild(AbstractLedgerState& child)
{
    mImpl->addChild(child);
}

void
LedgerState::Impl::addChild(AbstractLedgerState& child)
{
    checkNotSealed();
    checkNoChild();

    mChild = &child;
    mActive.clear();
    mActiveHeader.reset();
}

void
LedgerState::Impl::checkNoChild() const
{
    if (mChild)
    {
        throw std::runtime_error("LedgerState has child");
    }
}

void
LedgerState::Impl::checkNotSealed() const
{
    if (mIsSealed)
    {
        throw std::runtime_error("LedgerState is sealed");
    }
}

void
LedgerState::commit()
{
    mImpl->commit(getIdentifier());
    mImpl.reset();
}

void
LedgerState::Impl::commit(Identifier id)
{
    updateLastModified(); // Invokes checkNoChild

    mActive.clear();
    mActiveHeader.reset();
    mParent.commitChild(id);
}

void
LedgerState::commitChild(Identifier id)
{
    mImpl->commitChild(*this);
}

void
LedgerState::Impl::commitChild(LedgerState& self)
{
    auto entries = mChild->getEntries();
    for (auto const& kv : entries)
    {
        auto const& key = kv.first;
        if (kv.second)
        {
            mEntry[key] = std::make_shared<LedgerEntry>(*kv.second);
        }
        else
        {
            if (getNewestVersion(self, key).ledgerState == &self &&
                !mParent.getNewestVersion(key).entry)
            { // Created in this LedgerState
                mEntry.erase(key);
            }
            else
            { // Existed in a previous LedgerState
                mEntry[key] = nullptr;
            }
        }
    }
    mHeader = mChild->getHeader();

    mChild = nullptr;
}

LedgerStateEntry
LedgerState::create(LedgerEntry const& entry)
{
    return mImpl->create(*this, entry);
}

LedgerStateEntry
LedgerState::Impl::create(LedgerState& self, LedgerEntry const& entry)
{
    checkNotSealed();
    checkNoChild();

    auto key = LedgerEntryKey(entry);
    if (getNewestVersion(self, key).entry)
    {
        throw std::runtime_error("Key already exists");
    }

    auto current = std::make_shared<LedgerEntry>(entry);
    mEntry[key] = current;

    auto impl = std::make_shared<LedgerStateEntryImpl>(self, *current);
    mActive.emplace(key, impl);
    return LedgerStateEntry(impl);
}

void
LedgerState::deactivate(LedgerKey const& key)
{
    mImpl->deactivate(key);
}

void
LedgerState::Impl::deactivate(LedgerKey const& key)
{
    auto iter = mActive.find(key);
    assert(iter != mActive.end());
    mActive.erase(iter);
}

void
LedgerState::deactivateHeader()
{
    mImpl->deactivateHeader();
}

void
LedgerState::Impl::deactivateHeader()
{
    mActiveHeader.reset();
}

void
LedgerState::erase(LedgerKey const& key)
{
    mImpl->erase(*this, key);
}

void
LedgerState::Impl::erase(LedgerState const& self, LedgerKey const& key)
{
    checkNotSealed();
    checkNoChild();

    auto newest = getNewestVersion(self, key);
    if (!newest.entry)
    {
        throw std::runtime_error("Key does not exist");
    }
    if (mActive.find(key) != mActive.end())
    {
        throw std::runtime_error("Key is active");
    }

    if (newest.ledgerState == &self &&
        !mParent.getNewestVersion(key).entry)
    { // Created in this LedgerState
        mEntry.erase(key);
    }
    else
    { // Existed in a previous LedgerState
        mEntry[key] = nullptr;
    }
}

std::map<LedgerKey, LedgerEntry>
LedgerState::getAllOffers()
{
    return mImpl->getAllOffers();
}

std::map<LedgerKey, LedgerEntry>
LedgerState::Impl::getAllOffers()
{
    auto offers = mParent.getAllOffers();
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto const& entry = kv.second;
        if (!entry)
        {
            offers.erase(key);
            continue;
        }
        if (entry->data.type() != OFFER)
        {
            continue;
        }
        offers[key] = *entry;
    }
    return offers;
}

std::shared_ptr<LedgerEntry>
LedgerState::getBestOffer(Asset const& buying, Asset const& selling,
                          std::set<LedgerKey>&& exclude)
{
    return mImpl->getBestOffer(buying, selling, std::move(exclude));
}

std::shared_ptr<LedgerEntry>
LedgerState::Impl::getBestOffer(Asset const& buying, Asset const& selling,
                                std::set<LedgerKey>&& exclude)
{
    std::shared_ptr<LedgerEntry> bestOffer;
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto const& entry = kv.second;
        if (key.type() != OFFER)
        {
            continue;
        }

        if (!exclude.insert(key).second)
        {
            continue;
        }

        if (!(entry && entry->data.offer().buying == buying &&
              entry->data.offer().selling == selling))
        {
            continue;
        }

        if (!bestOffer)
        {
            bestOffer = std::make_shared<LedgerEntry>(*entry);
        }
        else if (isBetterOffer(*entry, *bestOffer))
        {
            *bestOffer = *entry;
        }
    }

    auto parentBestOffer = mParent.getBestOffer(buying, selling, std::move(exclude));
    if (bestOffer && parentBestOffer)
    {
        return isBetterOffer(*bestOffer, *parentBestOffer)
            ? bestOffer : parentBestOffer;
    }
    else
    {
        return bestOffer ? bestOffer : parentBestOffer;
    }
}

LedgerEntryChanges
LedgerState::getChanges()
{
    return mImpl->getChanges();
}

LedgerEntryChanges
LedgerState::Impl::getChanges()
{
    updateLastModified(); // Invokes checkNoChild

    LedgerEntryChanges changes;
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto const& entry = kv.second;

        auto previous = mParent.getNewestVersion(key);
        if (previous.entry)
        {
            changes.emplace_back(LEDGER_ENTRY_STATE);
            changes.back().state() = *previous.entry;

            if (entry)
            {
                changes.emplace_back(LEDGER_ENTRY_UPDATED);
                changes.back().updated() = *entry;
            }
            else
            {
                changes.emplace_back(LEDGER_ENTRY_REMOVED);
                changes.back().removed() = key;
            }
        }
        else
        {
            // If !entry and !previous.entry then the entry was created and
            // erased in this LedgerState, in which case it should not still be
            // in mEntry
            assert(entry);
            changes.emplace_back(LEDGER_ENTRY_CREATED);
            changes.back().created() = *entry;
        }
    }
    return changes;
}

std::vector<LedgerKey>
LedgerState::getDeadEntries()
{
    return mImpl->getDeadEntries();
}

std::vector<LedgerKey>
LedgerState::Impl::getDeadEntries()
{
    updateLastModified(); // Invokes checkNoChild

    std::vector<LedgerKey> res;
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto const& entry = kv.second;
        if (!entry)
        {
            res.push_back(key);
        }
    }
    return res;
}

LedgerStateDelta
LedgerState::getDelta()
{
    return mImpl->getDelta();
}

LedgerStateDelta
LedgerState::Impl::getDelta()
{
    updateLastModified(); // Invokes checkNoChild

    LedgerStateDelta delta;
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto previous = mParent.getNewestVersion(key);

        // Deep copy is not required here because getDelta causes LedgerState
        // to enter the sealed state, meaning subsequent modifications are
        // impossible.
        delta.entry[key] = {kv.second, previous.entry};
    }

    delta.header = {mHeader, mParent.getHeader()};
    return delta;
}

std::map<LedgerKey, std::shared_ptr<LedgerEntry const>>
LedgerState::getEntries() const
{
    return mImpl->getEntries();
}

std::map<LedgerKey, std::shared_ptr<LedgerEntry const>>
LedgerState::Impl::getEntries() const
{
    std::map<LedgerKey, std::shared_ptr<LedgerEntry const>> entries;
    for (auto const& kv : mEntry)
    {
        entries[kv.first] = kv.second;
    }
    return entries;
}

LedgerHeader const&
LedgerState::getHeader() const
{
    return mImpl->getHeader(*this);
}

LedgerHeader const&
LedgerState::Impl::getHeader(LedgerState const& self) const
{
    return mHeader;
}

std::vector<InflationWinner>
LedgerState::getInflationWinners(size_t maxWinners, int64_t minVotes)
{
    return mImpl->getInflationWinners(maxWinners, minVotes);
}

std::vector<InflationWinner>
LedgerState::Impl::getInflationWinners(size_t maxWinners, int64_t minVotes)
{
    // Calculate vote changes relative to parent
    std::map<AccountID, int64_t> deltaVotes;
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto const& entry = kv.second;
        if (key.type() != ACCOUNT)
        {
            continue;
        }

        if (entry)
        {
            auto const& acc = entry->data.account();
            if (acc.inflationDest && acc.balance >= 1000000000)
            {
                deltaVotes[*acc.inflationDest] += acc.balance;
            }
        }

        auto previous = mParent.getNewestVersion(key);
        if (previous.entry)
        {
            auto const& acc = previous.entry->data.account();
            if (acc.inflationDest && acc.balance >= 1000000000)
            {
                deltaVotes[*acc.inflationDest] -= acc.balance;
            }
        }
    }

    // Have to load extra winners corresponding to the number of accounts that
    // have had their vote totals change
    size_t numChanged =
        std::count_if(
            deltaVotes.begin(), deltaVotes.end(),
            [] (std::map<AccountID, int64_t>::value_type const& val) {
                return val.second != 0;
            });
    size_t newMaxWinners = maxWinners + numChanged;

    // Have to load accounts that could be winners after accounting for the
    // change in their vote totals
    int64_t maxIncrease =
        std::max_element(
            deltaVotes.begin(), deltaVotes.end(),
            [] (std::map<AccountID, int64_t>::value_type const& lhs,
                std::map<AccountID, int64_t>::value_type const& rhs) {
               return lhs.second < rhs.second;
            })->second;
    maxIncrease = std::max(int64_t(0), maxIncrease);
    int64_t newMinVotes = std::max(int64_t(0), minVotes - maxIncrease);

    // Get winners from parent, update votes, and add potential new winners
    // Note: It is possible that there are new winners in the case where an
    // account was receiving no votes before this ledger but now some accounts
    // are voting for it
    std::map<AccountID, int64_t> totalVotes;
    {
        auto winners = mParent.getInflationWinners(newMaxWinners, newMinVotes);
        for (auto const& winner : winners)
        {
            totalVotes[winner.accountID] = winner.votes;
        }
        for (auto const& delta : deltaVotes)
        {
            auto const& accountID = delta.first;
            auto const& voteDelta = delta.second;
            if ((totalVotes.find(accountID) != totalVotes.end()) ||
                voteDelta >= minVotes)
            {
                totalVotes[accountID] += voteDelta;
            }
        }
    }

    // Enumerate the new winners
    std::vector<InflationWinner> winners;
    for (auto const& total : totalVotes)
    {
        auto const& accountID = total.first;
        auto const& voteTotal = total.second;
        if (voteTotal >= minVotes)
        {
            winners.push_back({accountID, voteTotal});
        }
    }

    // Sort the new winners and remove the excess
    std::sort(
        winners.begin(), winners.end(),
        [] (InflationWinner const& lhs, InflationWinner const& rhs) {
            if (lhs.votes == rhs.votes)
            {
                return KeyUtils::toStrKey(lhs.accountID) >
                       KeyUtils::toStrKey(rhs.accountID);
            }
            return lhs.votes > rhs.votes;
        });
    if (winners.size() > maxWinners)
    {
        winners.resize(maxWinners);
    }
    return winners;
}

std::vector<LedgerEntry>
LedgerState::getLiveEntries()
{
    return mImpl->getLiveEntries();
}

std::vector<LedgerEntry>
LedgerState::Impl::getLiveEntries()
{
    updateLastModified(); // Invokes checkNoChild

    std::vector<LedgerEntry> res;
    for (auto const& kv : mEntry)
    {
        auto const& entry = kv.second;
        if (entry)
        {
            res.push_back(*entry);
        }
    }
    return res;
}

AbstractLedgerStateParent::EntryVersion
LedgerState::getNewestVersion(LedgerKey const& key) const
{
    return mImpl->getNewestVersion(*this, key);
}

AbstractLedgerStateParent::EntryVersion
LedgerState::Impl::getNewestVersion(LedgerState const& self,
                                    LedgerKey const& key) const
{
    auto iter = mEntry.find(key);
    if (iter != mEntry.end())
    {
        return {&self, iter->second};
    }
    return mParent.getNewestVersion(key);
}

std::map<LedgerKey, LedgerEntry>
LedgerState::getOffersByAccountAndAsset(AccountID const& account,
                                        Asset const& asset)
{
    return mImpl->getOffersByAccountAndAsset(account, asset);
}

std::map<LedgerKey, LedgerEntry>
LedgerState::Impl::getOffersByAccountAndAsset(AccountID const& account,
                                              Asset const& asset)
{
    auto offers = mParent.getOffersByAccountAndAsset(account, asset);
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto const& entry = kv.second;
        if (!entry)
        {
            offers.erase(key);
            continue;
        }
        if (entry->data.type() != OFFER)
        {
            continue;
        }

        auto const& oe = entry->data.offer();
        if (oe.sellerID == account &&
            (oe.selling == asset || oe.buying == asset))
        {
            offers[key] = *entry;
        }
    }
    return offers;
}

LedgerStateEntry
LedgerState::load(LedgerKey const& key)
{
    return mImpl->load(*this, key);
}

LedgerStateEntry
LedgerState::Impl::load(LedgerState& self, LedgerKey const& key)
{
    checkNotSealed();
    checkNoChild();
    if (mActive.find(key) != mActive.end())
    {
        throw std::runtime_error("Key already loaded");
    }

    auto newest = getNewestVersion(self, key);
    if (!newest.entry)
    {
        return {};
    }

    auto current = std::make_shared<LedgerEntry>(*newest.entry);
    mEntry[key] = current;

    auto impl = std::make_shared<LedgerStateEntryImpl>(self, *current);
    mActive.emplace(key, impl);
    return LedgerStateEntry(impl);
}

std::map<AccountID, std::vector<LedgerStateEntry>>
LedgerState::loadAllOffers()
{
    return mImpl->loadAllOffers(*this);
}

std::map<AccountID, std::vector<LedgerStateEntry>>
LedgerState::Impl::loadAllOffers(LedgerState& self)
{
    auto offers = getAllOffers();
    std::map<AccountID, std::vector<LedgerStateEntry>> offersByAccount;
    for (auto const& kv : offers)
    {
        auto const& key = kv.first;
        auto const& sellerID = key.offer().sellerID;
        offersByAccount[sellerID].emplace_back(load(self, key));
    }
    return offersByAccount;
}

LedgerStateEntry
LedgerState::loadBestOffer(Asset const& buying, Asset const& selling)
{
    return mImpl->loadBestOffer(*this, buying, selling);
}

LedgerStateEntry
LedgerState::Impl::loadBestOffer(LedgerState& self, Asset const& buying,
                                 Asset const& selling)
{
    checkNotSealed();
    checkNoChild();

    auto le = getBestOffer(buying, selling, {});
    return le ? load(self, LedgerEntryKey(*le)) : LedgerStateEntry();
}

LedgerStateHeader
LedgerState::loadHeader()
{
    return mImpl->loadHeader(*this);
}

LedgerStateHeader
LedgerState::Impl::loadHeader(LedgerState& self)
{
    checkNotSealed();
    checkNoChild();
    if (mActiveHeader)
    {
        throw std::runtime_error("LedgerStateHeader is active");
    }

    mActiveHeader = std::make_shared<HeaderImpl>(self, mHeader);
    return LedgerStateHeader(mActiveHeader);
}

std::vector<LedgerStateEntry>
LedgerState::loadOffersByAccountAndAsset(AccountID const& accountID,
                                         Asset const& asset)
{
    return mImpl->loadOffersByAccountAndAsset(*this, accountID, asset);
}

std::vector<LedgerStateEntry>
LedgerState::Impl::loadOffersByAccountAndAsset(LedgerState& self,
                                               AccountID const& accountID,
                                               Asset const& asset)
{
    checkNotSealed();
    checkNoChild();

    std::vector<LedgerStateEntry> res;
    auto offers = getOffersByAccountAndAsset(accountID, asset);
    for (auto const& kv : offers)
    {
        auto const& key = kv.first;
        res.emplace_back(load(self, key));
    }
    return res;
}

ConstLedgerStateEntry
LedgerState::loadWithoutRecord(LedgerKey const& key)
{
    return mImpl->loadWithoutRecord(*this, key);
}

ConstLedgerStateEntry
LedgerState::Impl::loadWithoutRecord(LedgerState& self, LedgerKey const& key)
{
    checkNotSealed();
    checkNoChild();
    if (mActive.find(key) != mActive.end())
    {
        throw std::runtime_error("Key already loaded");
    }

    auto newest = getNewestVersion(self, key);
    if (!newest.entry)
    {
        return {};
    }

    auto impl =
        std::make_shared<ConstLedgerStateEntryImpl>(self, *newest.entry);
    mActive.emplace(key, impl);
    return ConstLedgerStateEntry(impl);
}

void
LedgerState::rollback()
{
    mImpl->rollback(getIdentifier());
    mImpl.reset();
}

void
LedgerState::Impl::rollback(Identifier id)
{
    if (mChild)
    {
        mChild->rollback();
    }

    mActive.clear();
    mActiveHeader.reset();

    mParent.rollbackChild(id);
}

void
LedgerState::rollbackChild(Identifier id)
{
    mImpl->rollbackChild();
}

void
LedgerState::Impl::rollbackChild()
{
    mChild = nullptr;
}

void
LedgerState::unsealHeader(std::function<void(LedgerHeader&)> f)
{
    mImpl->unsealHeader(*this, f);
}

void
LedgerState::Impl::unsealHeader(
    LedgerState& self, std::function<void(LedgerHeader&)> f)
{
    if (!mIsSealed)
    {
        throw std::runtime_error("LedgerState is not sealed");
    }
    if (mActiveHeader)
    {
        throw std::runtime_error("LedgerStateHeader is active");
    }

    mActiveHeader = std::make_shared<HeaderImpl>(self, mHeader);
    LedgerStateHeader header(mActiveHeader);
    f(header.current());
}

void
LedgerState::Impl::updateLastModified()
{
    checkNoChild();

    mIsSealed = true;
    mActive.clear();
    mActiveHeader.reset();

    if (mShouldUpdateLastModified)
    {
        for (auto& kv : mEntry)
        {
            auto& entry = kv.second;
            if (entry)
            {
                entry->lastModifiedLedgerSeq = mHeader.ledgerSeq;
            }
        }
    }
}

// Implementation of LedgerStateRoot ------------------------------------------
LedgerStateRoot::LedgerStateRoot(Database& db, size_t cacheSize,
                                 size_t bestOfferCacheSize)
    : mImpl(std::make_unique<Impl>(db, cacheSize, bestOfferCacheSize))
{
}

LedgerStateRoot::Impl::Impl(Database& db, size_t cacheSize,
                            size_t bestOfferCacheSize)
    : mDatabase(db), mCacheSize(cacheSize)
    , mCache(std::make_unique<CacheType>(mCacheSize))
    , mBestOffersCache(
            std::make_unique<BestOffersCacheType>(bestOfferCacheSize))
    , mChild(nullptr)
{
}

void
LedgerStateRoot::addChild(AbstractLedgerState& child)
{
    mImpl->addChild(child);
}

void
LedgerStateRoot::Impl::addChild(AbstractLedgerState& child)
{
    if (mChild)
    {
        throw std::runtime_error("LedgerStateRoot already has child");
    }
    mChild = &child;
    mTransaction = std::make_unique<soci::transaction>(mDatabase.getSession());
}

void
LedgerStateRoot::commitChild(Identifier id)
{
    mImpl->commitChild(*this);
}

void
LedgerStateRoot::Impl::commitChild(LedgerStateRoot const& self)
{
    auto header = mChild->getHeader();
    auto entries = mChild->getEntries();

    mBestOffersCache->clear();

    try
    {
        for (auto const& kv : entries)
        {
            auto const& key = kv.first;
            auto const& entry = kv.second;
            switch (key.type())
            {
            case ACCOUNT:
                storeAccount(self, key, entry);
                break;
            case DATA:
                storeData(self, key, entry);
                break;
            case OFFER:
                storeOffer(self, key, entry);
                break;
            case TRUSTLINE:
                storeTrustLine(self, key, entry);
                break;
            default:
                throw std::runtime_error("Unknown key type");
            }
            auto cacheKey = binToHex(xdr::xdr_to_opaque(key));
            mCache->put(cacheKey, entry ? std::make_shared<LedgerEntry const>(*entry)
                                        : nullptr);
        }
    }
    catch (...)
    {
        mCache = std::make_unique<CacheType>(mCacheSize);
        throw;
    }

    mTransaction->commit();
    mTransaction.reset();
    mChild = nullptr;
    mHeader = header;
}

std::string
LedgerStateRoot::Impl::tableFromLedgerEntryType(LedgerEntryType let)
{
    switch (let)
    {
    case ACCOUNT:
        return "accounts";
    case DATA:
        return "accountdata";
    case OFFER:
        return "offers";
    case TRUSTLINE:
        return "trustlines";
    default:
        throw std::runtime_error("Unknown ledger entry type");
    }
}

uint64_t
LedgerStateRoot::countObjects(LedgerEntryType let) const
{
    return mImpl->countObjects(let);
}

uint64_t
LedgerStateRoot::Impl::countObjects(LedgerEntryType let) const
{
    using namespace soci;
    std::string query =
        "SELECT COUNT(*) FROM " + tableFromLedgerEntryType(let) + ";";
    uint64_t count = 0;
    mDatabase.getSession() << query, into(count);
    return count;
}

uint64_t
LedgerStateRoot::countObjects(LedgerEntryType let,
                              LedgerRange const& ledgers) const
{
    return mImpl->countObjects(let, ledgers);
}

uint64_t
LedgerStateRoot::Impl::countObjects(LedgerEntryType let,
                                    LedgerRange const& ledgers) const
{
    using namespace soci;
    std::string query =
        "SELECT COUNT(*) FROM " + tableFromLedgerEntryType(let) +
        " WHERE lastmodified >= :v1 AND lastmodified <= :v2;";
    uint64_t count = 0;
    mDatabase.getSession() << query, into(count), use(ledgers.first()),
        use(ledgers.last());
    return count;
}

void
LedgerStateRoot::deleteObjectsModifiedOnOrAfterLedger(
        uint32_t ledger) const
{
    return mImpl->deleteObjectsModifiedOnOrAfterLedger(ledger);
}

void
LedgerStateRoot::Impl::deleteObjectsModifiedOnOrAfterLedger(
        uint32_t ledger) const
{
    using namespace soci;

    mCache->clear();

    {
        std::string query =
            "DELETE FROM signers WHERE accountid IN"
            " (SELECT accountid FROM accounts WHERE lastmodified >= :v1)";
        mDatabase.getSession() << query, use(ledger);
    }

    for (auto let : {ACCOUNT, DATA, TRUSTLINE, OFFER})
    {
        std::string query =
            "DELETE FROM " + tableFromLedgerEntryType(let) +
            " WHERE lastmodified >= :v1";
        mDatabase.getSession() << query, use(ledger);
    }
}

void
LedgerStateRoot::dropAccounts()
{
    mImpl->dropAccounts();
}

void
LedgerStateRoot::dropData()
{
    mImpl->dropData();
}

void
LedgerStateRoot::dropOffers()
{
    mImpl->dropOffers();
}

void
LedgerStateRoot::dropTrustLines()
{
    mImpl->dropTrustLines();
}

std::map<LedgerKey, LedgerEntry>
LedgerStateRoot::getAllOffers()
{
    return mImpl->getAllOffers();
}

std::map<LedgerKey, LedgerEntry>
LedgerStateRoot::Impl::getAllOffers()
{
    auto offers = loadAllOffers();
    std::map<LedgerKey, LedgerEntry> offersByAccount;
    for (auto const& offer : offers)
    {
        offersByAccount[LedgerEntryKey(offer)] = offer;
    }
    return offersByAccount;
}

std::shared_ptr<LedgerEntry>
LedgerStateRoot::getBestOffer(Asset const& buying, Asset const& selling,
                              std::set<LedgerKey>&& exclude)
{
    return mImpl->getBestOffer(buying, selling, std::move(exclude));
}

std::shared_ptr<LedgerEntry>
LedgerStateRoot::Impl::getBestOffer(Asset const& buying, Asset const& selling,
                                    std::set<LedgerKey>&& exclude)
{
    std::string cacheKey = binToHex(xdr::xdr_to_opaque(buying))
                         + binToHex(xdr::xdr_to_opaque(selling));
    if (!mBestOffersCache->exists(cacheKey))
    {
        mBestOffersCache->put(cacheKey, {{}, false});
    }
    auto& cached = mBestOffersCache->get(cacheKey);

    auto& offers = cached.first;
    for (auto const& offer : offers)
    {
        auto key = LedgerEntryKey(offer);
        if (exclude.find(key) == exclude.end())
        {
            return std::make_shared<LedgerEntry>(offer);
        }
    }

    while (!cached.second)
    {
        auto newOffers = loadBestOffers(buying, selling, 5, offers.size());
        if (newOffers.size() < 5)
        {
            cached.second = true;
        }

        offers.insert(offers.end(), newOffers.begin(), newOffers.end());
        for (auto const& offer : newOffers)
        {
            auto key = LedgerEntryKey(offer);
            if (exclude.find(key) == exclude.end())
            {
                return std::make_shared<LedgerEntry>(offer);
            }
        }
    }

    return {};
}

std::map<LedgerKey, LedgerEntry>
LedgerStateRoot::getOffersByAccountAndAsset(AccountID const& account,
                                            Asset const& asset)
{
    return mImpl->getOffersByAccountAndAsset(account, asset);
}

std::map<LedgerKey, LedgerEntry>
LedgerStateRoot::Impl::getOffersByAccountAndAsset(AccountID const& account,
                                                  Asset const& asset)
{
    std::map<LedgerKey, LedgerEntry> res;
    auto offers = loadOffersByAccountAndAsset(account, asset);
    for (auto const& offer : offers)
    {
        res[LedgerEntryKey(offer)] = offer;
    }
    return res;
}

LedgerHeader const&
LedgerStateRoot::getHeader() const
{
    return mImpl->getHeader();
}

LedgerHeader const&
LedgerStateRoot::Impl::getHeader() const
{
    return mHeader;
}

std::vector<InflationWinner>
LedgerStateRoot::getInflationWinners(size_t maxWinners, int64_t minVotes)
{
    return mImpl->getInflationWinners(maxWinners, minVotes);
}

std::vector<InflationWinner>
LedgerStateRoot::Impl::getInflationWinners(size_t maxWinners, int64_t minVotes)
{
    return loadInflationWinners(maxWinners, minVotes);
}

AbstractLedgerStateParent::EntryVersion
LedgerStateRoot::getNewestVersion(LedgerKey const& key) const
{
    return mImpl->getNewestVersion(*this, key);
}

AbstractLedgerStateParent::EntryVersion
LedgerStateRoot::Impl::getNewestVersion(LedgerStateRoot const& self,
                                        LedgerKey const& key) const
{
    std::shared_ptr<LedgerEntry const> entry;
    auto cacheKey = binToHex(xdr::xdr_to_opaque(key));
    if (mCache->exists(cacheKey))
    {
        auto ptr = mCache->get(cacheKey);
        entry = ptr ? std::make_shared<LedgerEntry const>(*ptr.get()) : nullptr;
    }
    else
    {
        switch (key.type())
        {
        case ACCOUNT:
            entry = loadAccount(key);
            break;
        case DATA:
            entry = loadData(key);
            break;
        case OFFER:
            entry = loadOffer(key);
            break;
        case TRUSTLINE:
            entry = loadTrustLine(key);
            break;
        default:
            throw std::runtime_error("Unknown key type");
        }
        mCache->put(cacheKey, entry);
    }

    if (entry)
    {
        return {&self, entry};
    }
    return {nullptr, nullptr};
}

void
LedgerStateRoot::rollbackChild(Identifier id)
{
    mImpl->rollbackChild();
}

void
LedgerStateRoot::Impl::rollbackChild()
{
    mTransaction->rollback();
    mTransaction.reset();
    mChild = nullptr;
}

void
LedgerStateRoot::Impl::storeAccount(
    LedgerStateRoot const& self, LedgerKey const& key,
    std::shared_ptr<LedgerEntry const> const& entry)
{
    auto const previous = getNewestVersion(self, key).entry;
    if (entry)
    {
        insertOrUpdateAccount(*entry, !previous);
        storeSigners(*entry, previous);
    }
    else if (previous) // Deleted
    {
        deleteAccount(key);
    }
}

void
LedgerStateRoot::Impl::storeData(
    LedgerStateRoot const& self, LedgerKey const& key,
    std::shared_ptr<LedgerEntry const> const& entry)
{
    auto const previous = getNewestVersion(self, key).entry;
    if (entry)
    {
        insertOrUpdateData(*entry, !previous);
    }
    else if (previous) // Deleted
    {
        deleteData(key);
    }
}

void
LedgerStateRoot::Impl::storeOffer(
    LedgerStateRoot const& self, LedgerKey const& key,
    std::shared_ptr<LedgerEntry const> const& entry)
{
    auto const previous = getNewestVersion(self, key).entry;
    if (entry)
    {
        insertOrUpdateOffer(*entry, !previous);
    }
    else if (previous) // Deleted
    {
        deleteOffer(key);
    }
}

void
LedgerStateRoot::Impl::storeTrustLine(
    LedgerStateRoot const& self, LedgerKey const& key,
    std::shared_ptr<LedgerEntry const> const& entry)
{
    auto const previous = getNewestVersion(self, key).entry;
    if (entry)
    {
        insertOrUpdateTrustLine(*entry, !previous);
    }
    else if (previous) // Deleted
    {
        deleteTrustLine(key);
    }
}
}
