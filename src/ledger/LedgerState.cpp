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
#include "ledger/LedgerStateImpl.h"
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

// Implementation of AbstractLedgerStateParent --------------------------------
AbstractLedgerStateParent::~AbstractLedgerStateParent()
{
}

AbstractLedgerStateParent::Identifier
AbstractLedgerStateParent::getIdentifier() const
{
    return Identifier();
}

// Implementation of EntryIterator --------------------------------------------
AbstractLedgerState::EntryIterator::EntryIterator(std::unique_ptr<AbstractImpl>&& impl)
    : mImpl(std::move(impl))
{
}

AbstractLedgerState::EntryIterator::EntryIterator(EntryIterator&& other)
    : mImpl(std::move(other.mImpl))
{
}

AbstractLedgerState::EntryIterator&
AbstractLedgerState::EntryIterator::operator++()
{
    mImpl->advance();
    return *this;
}

AbstractLedgerState::EntryIterator::operator bool() const
{
    return !mImpl->atEnd();
}

LedgerEntry const&
AbstractLedgerState::EntryIterator::entry() const
{
    return mImpl->entry();
}

bool
AbstractLedgerState::EntryIterator::entryExists() const
{
    return mImpl->entryExists();
}

LedgerKey const&
AbstractLedgerState::EntryIterator::key() const
{
    return mImpl->key();
}

// Implementation of AbstractLedgerState --------------------------------------
AbstractLedgerState::~AbstractLedgerState()
{
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
    sealAndMaybeUpdateLastModified(); // Invokes checkNoChild

    mActive.clear();
    mActiveHeader.reset();
    mParent.commitChild(id);
}

void
LedgerState::commitChild(Identifier id)
{
    mImpl->commitChild();
}

void
LedgerState::Impl::commitChild()
{
    for (auto iter = mChild->getEntryIterator(); (bool)iter; ++iter)
    {
        auto const& key = iter.key();
        if (iter.entryExists())
        {
            mEntry[key] = std::make_shared<LedgerEntry>(iter.entry());
        }
        else
        {
            // TODO(jonjove): Check that this is correct
            if (!mParent.getNewestVersion(key))
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
    if (getNewestVersion(key))
    {
        throw std::runtime_error("Key already exists");
    }

    auto current = std::make_shared<LedgerEntry>(entry);
    mEntry[key] = current;

    auto impl = LedgerStateEntry::makeSharedImpl(self, *current);
    mActive.emplace(key, toEntryImplBase(impl));
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
    if (iter == mActive.end())
    {
        throw std::runtime_error("Key does not exist");
    }
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
    mImpl->erase(key);
}

void
LedgerState::Impl::erase(LedgerKey const& key)
{
    checkNotSealed();
    checkNoChild();

    auto newest = getNewestVersion(key);
    if (!newest)
    {
        throw std::runtime_error("Key does not exist");
    }
    if (mActive.find(key) != mActive.end())
    {
        throw std::runtime_error("Key is active");
    }

    if (!mParent.getNewestVersion(key))
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
    auto end = mEntry.end();
    auto bestOfferIter = end;
    for (auto iter = mEntry.begin(); iter != end; ++iter)
    {
        auto const& key = iter->first;
        auto const& entry = iter->second;
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

        if ((bestOfferIter == end) ||
            isBetterOffer(*entry, *bestOfferIter->second))
        {
            bestOfferIter = iter;
        }
    }

    std::shared_ptr<LedgerEntry> bestOffer;
    if (bestOfferIter != end)
    {
        bestOffer = std::make_shared<LedgerEntry>(*bestOfferIter->second);
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
    sealAndMaybeUpdateLastModified(); // Invokes checkNoChild

    LedgerEntryChanges changes;
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto const& entry = kv.second;

        auto previous = mParent.getNewestVersion(key);
        if (previous)
        {
            changes.emplace_back(LEDGER_ENTRY_STATE);
            changes.back().state() = *previous;

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
    sealAndMaybeUpdateLastModified(); // Invokes checkNoChild

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
    sealAndMaybeUpdateLastModified(); // Invokes checkNoChild

    LedgerStateDelta delta;
    for (auto const& kv : mEntry)
    {
        auto const& key = kv.first;
        auto previous = mParent.getNewestVersion(key);

        // Deep copy is not required here because getDelta causes LedgerState
        // to enter the sealed state, meaning subsequent modifications are
        // impossible.
        delta.entry[key] = {kv.second, previous};
    }

    delta.header = {mHeader, mParent.getHeader()};
    return delta;
}

AbstractLedgerState::EntryIterator
LedgerState::getEntryIterator() const
{
    return mImpl->getEntryIterator();
}

AbstractLedgerState::EntryIterator
LedgerState::Impl::getEntryIterator() const
{
    auto iterImpl = std::make_unique<EntryIteratorImpl>(mEntry.begin(), mEntry.end());
    return AbstractLedgerState::EntryIterator(std::move(iterImpl));
}

LedgerHeader const&
LedgerState::getHeader() const
{
    return mImpl->getHeader();
}

LedgerHeader const&
LedgerState::Impl::getHeader() const
{
    return mHeader;
}

std::vector<InflationWinner>
LedgerState::getInflationWinners(size_t maxWinners, int64_t minVotes)
{
    return mImpl->getInflationWinners(maxWinners, minVotes);
}

std::map<AccountID, int64_t>
LedgerState::Impl::getDeltaVotes() const
{
    int64_t const MIN_VOTES_TO_INCLUDE = 1000000000;
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
            if (acc.inflationDest && acc.balance >= MIN_VOTES_TO_INCLUDE)
            {
                deltaVotes[*acc.inflationDest] += acc.balance;
            }
        }

        auto previous = mParent.getNewestVersion(key);
        if (previous)
        {
            auto const& acc = previous->data.account();
            if (acc.inflationDest && acc.balance >= MIN_VOTES_TO_INCLUDE)
            {
                deltaVotes[*acc.inflationDest] -= acc.balance;
            }
        }
    }
    return deltaVotes;
}

std::map<AccountID, int64_t>
LedgerState::Impl::getTotalVotes(
    std::vector<InflationWinner> const& parentWinners,
    std::map<AccountID, int64_t> const& deltaVotes, int64_t minVotes) const
{
    std::map<AccountID, int64_t> totalVotes;
    for (auto const& winner : parentWinners)
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
    return totalVotes;
}

std::vector<InflationWinner>
LedgerState::Impl::enumerateInflationWinners(
    std::map<AccountID, int64_t> const& totalVotes,
    size_t maxWinners, int64_t minVotes) const
{
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
    std::sort(winners.begin(), winners.end(),
        [] (auto const& lhs, auto const& rhs) {
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

std::vector<InflationWinner>
LedgerState::Impl::getInflationWinners(size_t maxWinners, int64_t minVotes)
{
    // Calculate vote changes relative to parent
    auto deltaVotes = getDeltaVotes();

    // Have to load extra winners corresponding to the number of accounts that
    // have had their vote totals change
    size_t numChanged =
        std::count_if(deltaVotes.begin(), deltaVotes.end(),
            [] (auto const& val) {
                return val.second != 0;
            });
    size_t newMaxWinners = maxWinners + numChanged;

    // Have to load accounts that could be winners after accounting for the
    // change in their vote totals
    int64_t maxIncrease =
        std::max_element(deltaVotes.begin(), deltaVotes.end(),
            [] (auto const& lhs, auto const& rhs) {
               return lhs.second < rhs.second;
            })->second;
    maxIncrease = std::max(int64_t(0), maxIncrease);
    int64_t newMinVotes = std::max(int64_t(0), minVotes - maxIncrease);

    // Get winners from parent, update votes, and add potential new winners
    // Note: It is possible that there are new winners in the case where an
    // account was receiving no votes before this ledger but now some accounts
    // are voting for it
    auto totalVotes =
        getTotalVotes(mParent.getInflationWinners(newMaxWinners, newMinVotes),
                      deltaVotes, minVotes);

    // Enumerate the new winners in sorted order
    return enumerateInflationWinners(totalVotes, maxWinners, minVotes);
}

std::vector<LedgerEntry>
LedgerState::getLiveEntries()
{
    return mImpl->getLiveEntries();
}

std::vector<LedgerEntry>
LedgerState::Impl::getLiveEntries()
{
    sealAndMaybeUpdateLastModified(); // Invokes checkNoChild

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

std::shared_ptr<LedgerEntry const>
LedgerState::getNewestVersion(LedgerKey const& key) const
{
    return mImpl->getNewestVersion(key);
}

std::shared_ptr<LedgerEntry const>
LedgerState::Impl::getNewestVersion(LedgerKey const& key) const
{
    auto iter = mEntry.find(key);
    if (iter != mEntry.end())
    {
        return iter->second;
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
        if (key.type() != OFFER)
        {
            continue;
        }
        if (!entry)
        {
            offers.erase(key);
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
        throw std::runtime_error("Key is active");
    }

    auto newest = getNewestVersion(key);
    if (!newest)
    {
        return {};
    }

    auto current = std::make_shared<LedgerEntry>(*newest);
    mEntry[key] = current;

    auto impl = LedgerStateEntry::makeSharedImpl(self, *current);
    mActive.emplace(key, toEntryImplBase(impl));
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

    mActiveHeader = LedgerStateHeader::makeSharedImpl(self, mHeader);
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
        throw std::runtime_error("Key is active");
    }

    auto newest = getNewestVersion(key);
    if (!newest)
    {
        return {};
    }

    auto impl = ConstLedgerStateEntry::makeSharedImpl(self, *newest);
    mActive.emplace(key, toEntryImplBase(impl));
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

    mActiveHeader = LedgerStateHeader::makeSharedImpl(self, mHeader);
    LedgerStateHeader header(mActiveHeader);
    f(header.current());
}

void
LedgerState::Impl::sealAndMaybeUpdateLastModified()
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
LedgerStateRoot::LedgerStateRoot(Database& db, size_t entryCacheSize,
                                 size_t bestOfferCacheSize)
    : mImpl(std::make_unique<Impl>(db, entryCacheSize, bestOfferCacheSize))
{
}

LedgerStateRoot::Impl::Impl(Database& db, size_t entryCacheSize,
                            size_t bestOfferCacheSize)
    : mDatabase(db), mEntryCacheSize(entryCacheSize)
    , mEntryCache(std::make_unique<EntryCacheType>(mEntryCacheSize))
    , mBestOffersCache(
            std::make_unique<BestOffersCacheType>(bestOfferCacheSize))
    , mChild(nullptr)
{
}

LedgerStateRoot::~LedgerStateRoot()
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
LedgerStateRoot::Impl::checkNoChild() const
{
    if (mChild)
    {
        throw std::runtime_error("LedgerStateRoot has child");
    }
}

void
LedgerStateRoot::commitChild(Identifier id)
{
    mImpl->commitChild();
}

void
LedgerStateRoot::Impl::commitChild()
{
    auto header = mChild->getHeader();

    mBestOffersCache->clear();

    try
    {
        for (auto iter = mChild->getEntryIterator(); (bool)iter; ++iter)
        {
            auto const& key = iter.key();
            switch (key.type())
            {
            case ACCOUNT:
                storeAccount(iter);
                break;
            case DATA:
                storeData(iter);
                break;
            case OFFER:
                storeOffer(iter);
                break;
            case TRUSTLINE:
                storeTrustLine(iter);
                break;
            default:
                throw std::runtime_error("Unknown key type");
            }
            auto cacheKey = binToHex(xdr::xdr_to_opaque(key));
            mEntryCache->put(cacheKey, iter.entryExists()
                ? std::make_shared<LedgerEntry const>(iter.entry()) : nullptr);
        }
    }
    catch (...)
    {
        mEntryCache = std::make_unique<EntryCacheType>(mEntryCacheSize);
        throw;
    }

    mTransaction->commit();
    mTransaction.reset();
    mChild = nullptr;
    mHeader = header;

    mDatabase.clearPreparedStatementCache();
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
    checkNoChild();

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
    checkNoChild();

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
    checkNoChild();
    mEntryCache->clear();
    mBestOffersCache->clear();

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

    auto& offers = cached.bestOffers;
    for (auto const& offer : offers)
    {
        auto key = LedgerEntryKey(offer);
        if (exclude.find(key) == exclude.end())
        {
            return std::make_shared<LedgerEntry>(offer);
        }
    }

    size_t const BATCH_SIZE = 5;
    while (!cached.allLoaded)
    {
        auto newOffers =
            loadBestOffers(buying, selling, BATCH_SIZE, offers.size());
        if (newOffers.size() < BATCH_SIZE)
        {
            cached.allLoaded = true;
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
        res.emplace(LedgerEntryKey(offer), offer);
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

std::shared_ptr<LedgerEntry const>
LedgerStateRoot::getNewestVersion(LedgerKey const& key) const
{
    return mImpl->getNewestVersion(key);
}

std::shared_ptr<LedgerEntry const>
LedgerStateRoot::Impl::getNewestVersion(LedgerKey const& key) const
{
    std::shared_ptr<LedgerEntry const> entry;
    auto cacheKey = binToHex(xdr::xdr_to_opaque(key));
    if (mEntryCache->exists(cacheKey))
    {
        auto ptr = mEntryCache->get(cacheKey);
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
        mEntryCache->put(cacheKey, entry);
    }

    return entry;
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
LedgerStateRoot::Impl::storeAccount(AbstractLedgerState::EntryIterator const& iter)
{
    if (iter.entryExists())
    {
        auto const previous = getNewestVersion(iter.key());
        insertOrUpdateAccount(iter.entry(), !previous);
        storeSigners(iter.entry(), previous);
    }
    else
    {
        deleteAccount(iter.key());
    }
}

void
LedgerStateRoot::Impl::storeData(AbstractLedgerState::EntryIterator const& iter)
{
    if (iter.entryExists())
    {
        auto const previous = getNewestVersion(iter.key());
        insertOrUpdateData(iter.entry(), !previous);
    }
    else
    {
        deleteData(iter.key());
    }
}

void
LedgerStateRoot::Impl::storeOffer(AbstractLedgerState::EntryIterator const& iter)
{
    if (iter.entryExists())
    {
        auto const previous = getNewestVersion(iter.key());
        insertOrUpdateOffer(iter.entry(), !previous);
    }
    else
    {
        deleteOffer(iter.key());
    }
}

void
LedgerStateRoot::Impl::storeTrustLine(AbstractLedgerState::EntryIterator const& iter)
{
    if (iter.entryExists())
    {
        auto const previous = getNewestVersion(iter.key());
        insertOrUpdateTrustLine(iter.entry(), !previous);
    }
    else
    {
        deleteTrustLine(iter.key());
    }
}
}
