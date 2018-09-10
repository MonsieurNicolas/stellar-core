// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/ManageOfferOpFrame.h"
#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerState.h"
#include "ledger/LedgerStateEntry.h"
#include "ledger/LedgerStateHeader.h"
#include "ledger/TrustLineWrapper.h"
#include "ledger/OfferFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "transactions/TransactionUtils.h"
#include "util/Logging.h"
#include "util/XDROperators.h"
#include "util/types.h"

// convert from sheep to wheat
// selling sheep
// buying wheat

namespace stellar
{

using namespace std;

ManageOfferOpFrame::ManageOfferOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageOffer(mOperation.body.manageOfferOp())
{
    mPassive = false;
}

// make sure these issuers exist and you can hold the ask asset
bool
ManageOfferOpFrame::checkOfferValid(medida::MetricsRegistry& metrics,
                                    Database& db, LedgerDelta& delta)
{
    Asset const& sheep = mManageOffer.selling;
    Asset const& wheat = mManageOffer.buying;

    if (mManageOffer.amount == 0)
    {
        // don't bother loading trust lines as we're deleting the offer
        return true;
    }

    if (sheep.type() != ASSET_TYPE_NATIVE)
    {
        auto tlI =
            TrustFrame::loadTrustLineIssuer(getSourceID(), sheep, db, delta);
        mSheepLineA = tlI.first;
        if (!tlI.second)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_SELL_NO_ISSUER);
            return false;
        }
        if (!mSheepLineA)
        { // we don't have what we are trying to sell
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_SELL_NO_TRUST);
            return false;
        }
        if (mSheepLineA->getBalance() == 0)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "underfunded"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_UNDERFUNDED);
            return false;
        }
        if (!mSheepLineA->isAuthorized())
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-not-authorized"},
                          "operation")
                .Mark();
            // we are not authorized to sell
            innerResult().code(MANAGE_OFFER_SELL_NOT_AUTHORIZED);
            return false;
        }
    }

    if (wheat.type() != ASSET_TYPE_NATIVE)
    {
        auto tlI =
            TrustFrame::loadTrustLineIssuer(getSourceID(), wheat, db, delta);
        mWheatLineA = tlI.first;
        if (!tlI.second)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_BUY_NO_ISSUER);
            return false;
        }
        if (!mWheatLineA)
        { // we can't hold what we are trying to buy
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_BUY_NO_TRUST);
            return false;
        }
        if (!mWheatLineA->isAuthorized())
        { // we are not authorized to hold what we
            // are trying to buy
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-not-authorized"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_BUY_NOT_AUTHORIZED);
            return false;
        }
    }
    return true;
}

// you are selling sheep for wheat
// need to check the counter offers selling wheat for sheep
// see if this is modifying an old offer
// see if this offer crosses any existing offers
bool
ManageOfferOpFrame::doApply(Application& app, LedgerDelta& delta,
                            LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    if (!checkOfferValid(app.getMetrics(), db, delta))
    {
        return false;
    }

    Asset const& sheep = mManageOffer.selling;
    Asset const& wheat = mManageOffer.buying;

    bool creatingNewOffer = false;
    uint64_t offerID = mManageOffer.offerID;

    soci::transaction sqlTx(db.getSession());
    LedgerDelta tempDelta(delta);

    if (offerID)
    { // modifying an old offer
        mSellSheepOffer =
            OfferFrame::loadOffer(getSourceID(), offerID, db, &tempDelta);

        if (!mSellSheepOffer)
        {
            app.getMetrics()
                .NewMeter({"op-manage-offer", "invalid", "not-found"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_NOT_FOUND);
            return false;
        }

        // We are releasing the liabilites associated with this offer. This is
        // required in order to produce available balance for the offer to be
        // executed. Both trust lines must be reset since it is possible that
        // the assets are updated (including the edge case that the buying and
        // selling assets are swapped).
        if (ledgerManager.getCurrentLedgerVersion() >= 10)
        {
            mWheatLineA.reset();
            mSheepLineA.reset();

            mSellSheepOffer->releaseLiabilities(
                mSourceAccount, nullptr, nullptr, tempDelta, db, ledgerManager);

            if (sheep.type() != ASSET_TYPE_NATIVE)
            {
                mSheepLineA = TrustFrame::loadTrustLine(getSourceID(), sheep,
                                                        db, &tempDelta);
            }
            if (wheat.type() != ASSET_TYPE_NATIVE)
            {
                mWheatLineA = TrustFrame::loadTrustLine(getSourceID(), wheat,
                                                        db, &tempDelta);
            }
        }

        // WARNING: mSellSheepOffer is deleted but mSourceAccount is not updated
        // to reflect the change in numSubEntries at this point. However, we
        // can't delete it here since doing so would modify mSourceAccount,
        // which would lead to different buckets being generated.
        mSellSheepOffer->storeDelete(tempDelta, db);

        // rebuild offer based off the manage offer
        mSellSheepOffer->getOffer() = buildOffer(
            getSourceID(), mManageOffer, mSellSheepOffer->getOffer().flags);
        mPassive = mSellSheepOffer->getFlags() & PASSIVE_FLAG;
    }
    else
    { // creating a new Offer
        creatingNewOffer = true;
        LedgerEntry le;
        le.data.type(OFFER);
        le.data.offer() = buildOffer(getSourceID(), mManageOffer,
                                     mPassive ? PASSIVE_FLAG : 0);
        mSellSheepOffer = std::make_shared<OfferFrame>(le);
    }

    innerResult().code(MANAGE_OFFER_SUCCESS);

    bool adjusted = false;

    if (mManageOffer.amount == 0)
    {
        // deleting the offer
        mSellSheepOffer->getOffer().amount = 0;
    }
    else
    {
        auto ledgerVersion = app.getLedgerManager().getCurrentLedgerVersion();
        if (creatingNewOffer &&
            (ledgerVersion >= 10 ||
             (sheep.type() == ASSET_TYPE_NATIVE && ledgerVersion > 8)))
        {
            // we need to compute maxAmountOfSheepCanSell based on the
            // updated reserve to avoid selling too many and falling
            // below the reserve when we try to create the offer later on
            if (!mSourceAccount->addNumEntries(1, ledgerManager))
            {
                app.getMetrics()
                    .NewMeter({"op-manage-offer", "invalid", "low reserve"},
                              "operation")
                    .Mark();
                innerResult().code(MANAGE_OFFER_LOW_RESERVE);
                return false;
            }
            adjusted = true;
        }

        Price const& sheepPrice = mSellSheepOffer->getPrice();
        const Price maxWheatPrice(sheepPrice.d, sheepPrice.n);

        int64_t maxWheatReceive =
            canBuyAtMost(mSourceAccount, wheat, mWheatLineA, ledgerManager);
        int64_t maxSheepSend;
        if (app.getLedgerManager().getCurrentLedgerVersion() >= 10)
        {
            int64_t availableLimit =
                (wheat.type() == ASSET_TYPE_NATIVE)
                    ? mSourceAccount->getMaxAmountReceive(ledgerManager)
                    : mWheatLineA->getMaxAmountReceive(ledgerManager);
            if (availableLimit < mSellSheepOffer->getBuyingLiabilities())
            {
                app.getMetrics()
                    .NewMeter({"op-manage-offer", "invalid", "line-full"},
                              "operation")
                    .Mark();
                innerResult().code(MANAGE_OFFER_LINE_FULL);
                return false;
            }

            int64_t availableBalance =
                (sheep.type() == ASSET_TYPE_NATIVE)
                    ? mSourceAccount->getAvailableBalance(ledgerManager)
                    : mSheepLineA->getAvailableBalance(ledgerManager);
            if (availableBalance < mSellSheepOffer->getSellingLiabilities())
            {
                app.getMetrics()
                    .NewMeter({"op-manage-offer", "invalid", "underfunded"},
                              "operation")
                    .Mark();
                innerResult().code(MANAGE_OFFER_UNDERFUNDED);
                return false;
            }

            maxSheepSend = canSellAtMost(mSourceAccount, sheep, mSheepLineA,
                                         ledgerManager);
        }
        else
        {
            int64_t maxSheepCanSell = canSellAtMost(mSourceAccount, sheep,
                                                    mSheepLineA, ledgerManager);
            int64_t maxSheepBasedOnWheat;
            if (!bigDivide(maxSheepBasedOnWheat, maxWheatReceive, sheepPrice.d,
                           sheepPrice.n, ROUND_DOWN))
            {
                maxSheepBasedOnWheat = INT64_MAX;
            }

            maxSheepSend = std::min({maxSheepCanSell, maxSheepBasedOnWheat});
        }
        // amount of sheep for sale is the lesser of amount we can sell and
        // amount put in the offer
        maxSheepSend = std::min(mSellSheepOffer->getAmount(), maxSheepSend);

        if (adjusted)
        {
            // restore the number back (will be re-incremented later if
            // the offer really needs to be created)
            mSourceAccount->addNumEntries(-1, ledgerManager);
        }

        if (maxWheatReceive == 0)
        {
            app.getMetrics()
                .NewMeter({"op-manage-offer", "invalid", "line-full"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_LINE_FULL);
            return false;
        }

        int64_t sheepSent, wheatReceived;
        OfferExchange oe(tempDelta, ledgerManager);
        OfferExchange::ConvertResult r = oe.convertWithOffers(
            sheep, maxSheepSend, sheepSent, wheat, maxWheatReceive,
            wheatReceived, false, [this, &maxWheatPrice](OfferFrame const& o) {
                assert(o.getOfferID() != mSellSheepOffer->getOfferID());
                if ((mPassive && (o.getPrice() >= maxWheatPrice)) ||
                    (o.getPrice() > maxWheatPrice))
                {
                    return OfferExchange::eStop;
                }
                if (o.getSellerID() == getSourceID())
                {
                    // we are crossing our own offer
                    innerResult().code(MANAGE_OFFER_CROSS_SELF);
                    return OfferExchange::eStop;
                }
                return OfferExchange::eKeep;
            });
        assert(sheepSent >= 0);

        bool sheepStays;
        switch (r)
        {
        case OfferExchange::eOK:
            sheepStays = false;
            break;
        case OfferExchange::ePartial:
            sheepStays = true;
            break;
        case OfferExchange::eFilterStop:
            if (innerResult().code() != MANAGE_OFFER_SUCCESS)
            {
                return false;
            }
            sheepStays = true;
            break;
        default:
            abort();
        }

        // updates the result with the offers that got taken on the way

        for (auto const& oatom : oe.getOfferTrail())
        {
            innerResult().success().offersClaimed.push_back(oatom);
        }

        if (wheatReceived > 0)
        {
            // it's OK to use mSourceAccount, mWheatLineA and mSheepLineA
            // here as OfferExchange won't cross offers from source account
            if (wheat.type() == ASSET_TYPE_NATIVE)
            {
                if (!mSourceAccount->addBalance(wheatReceived, ledgerManager))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer claimed over limit");
                }

                mSourceAccount->storeChange(tempDelta, db);
            }
            else
            {
                if (!mWheatLineA->addBalance(wheatReceived, ledgerManager))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer claimed over limit");
                }

                mWheatLineA->storeChange(tempDelta, db);
            }

            if (sheep.type() == ASSET_TYPE_NATIVE)
            {
                if (!mSourceAccount->addBalance(-sheepSent, ledgerManager))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer sold more than balance");
                }
                mSourceAccount->storeChange(tempDelta, db);
            }
            else
            {
                if (!mSheepLineA->addBalance(-sheepSent, ledgerManager))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer sold more than balance");
                }
                mSheepLineA->storeChange(tempDelta, db);
            }
        }

        mSellSheepOffer->getOffer().amount = maxSheepSend - sheepSent;
        if (ledgerManager.getCurrentLedgerVersion() >= 10)
        {
            if (sheepStays)
            {
                adjustOffer(*mSellSheepOffer, ledgerManager, mSourceAccount,
                            sheep, mSheepLineA, wheat, mWheatLineA);
            }
            else
            {
                mSellSheepOffer->getOffer().amount = 0;
            }
        }
    }

    if (mSellSheepOffer->getOffer().amount > 0)
    { // we still have sheep to sell so leave an offer
        if (creatingNewOffer)
        {
            // make sure we don't allow us to add offers when we don't have
            // the minbalance (should never happen at this stage in v9+)
            if (!mSourceAccount->addNumEntries(1, ledgerManager))
            {
                app.getMetrics()
                    .NewMeter({"op-manage-offer", "invalid", "low reserve"},
                              "operation")
                    .Mark();
                innerResult().code(MANAGE_OFFER_LOW_RESERVE);
                return false;
            }
            mSellSheepOffer->mEntry.data.offer().offerID =
                tempDelta.getHeaderFrame().generateID();
            innerResult().success().offer.effect(MANAGE_OFFER_CREATED);
            mSourceAccount->storeChange(tempDelta, db);
        }
        else
        {
            innerResult().success().offer.effect(MANAGE_OFFER_UPDATED);
        }
        mSellSheepOffer->storeAdd(tempDelta, db);
        innerResult().success().offer.offer() = mSellSheepOffer->getOffer();

        if (ledgerManager.getCurrentLedgerVersion() >= 10)
        {
            mSellSheepOffer->acquireLiabilities(mSourceAccount, mWheatLineA,
                                                mSheepLineA, tempDelta, db,
                                                ledgerManager);
        }
    }
    else
    {
        innerResult().success().offer.effect(MANAGE_OFFER_DELETED);

        if (!creatingNewOffer)
        {
            mSourceAccount->addNumEntries(-1, ledgerManager);
            mSourceAccount->storeChange(tempDelta, db);
        }
    }

    sqlTx.commit();
    tempDelta.commit();

    app.getMetrics()
        .NewMeter({"op-create-offer", "success", "apply"}, "operation")
        .Mark();
    return true;
}

// make sure these issuers exist and you can hold the ask asset
bool
ManageOfferOpFrame::checkOfferValid(medida::MetricsRegistry& metrics,
                                    AbstractLedgerState& lsOuter)
{
    LedgerState ls(lsOuter); // ls will always be rolled back
    Asset const& sheep = mManageOffer.selling;
    Asset const& wheat = mManageOffer.buying;

    if (mManageOffer.amount == 0)
    {
        // don't bother loading trust lines as we're deleting the offer
        return true;
    }

    if (sheep.type() != ASSET_TYPE_NATIVE)
    {
        auto sheepLineA = loadTrustLine(ls, getSourceID(), sheep);
        auto issuer = stellar::loadAccount(ls, getIssuer(sheep));
        if (!issuer)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_SELL_NO_ISSUER);
            return false;
        }
        if (!sheepLineA)
        { // we don't have what we are trying to sell
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_SELL_NO_TRUST);
            return false;
        }
        if (sheepLineA.getBalance() == 0)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "underfunded"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_UNDERFUNDED);
            return false;
        }
        if (!sheepLineA.isAuthorized())
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-not-authorized"},
                          "operation")
                .Mark();
            // we are not authorized to sell
            innerResult().code(MANAGE_OFFER_SELL_NOT_AUTHORIZED);
            return false;
        }
    }

    if (wheat.type() != ASSET_TYPE_NATIVE)
    {
        auto wheatLineA = loadTrustLine(ls, getSourceID(), wheat);
        auto issuer = stellar::loadAccount(ls, getIssuer(wheat));
        if (!issuer)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_BUY_NO_ISSUER);
            return false;
        }
        if (!wheatLineA)
        { // we can't hold what we are trying to buy
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_BUY_NO_TRUST);
            return false;
        }
        if (!wheatLineA.isAuthorized())
        { // we are not authorized to hold what we
            // are trying to buy
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-not-authorized"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_BUY_NOT_AUTHORIZED);
            return false;
        }
    }
    return true;
}

bool
ManageOfferOpFrame::computeOfferExchangeParameters(
        Application& app, AbstractLedgerState& lsOuter,
        LedgerEntry const& offerEntry, bool creatingNewOffer,
        int64_t& maxSheepSend, int64_t& maxWheatReceive)
{
    LedgerState ls(lsOuter); // ls will always be rolled back

    auto const& offer = offerEntry.data.offer();
    Asset const& sheep = offer.selling;
    Asset const& wheat = offer.buying;

    auto header = ls.loadHeader();
    auto ledgerVersion = header.current().ledgerVersion;

    auto sourceAccount = loadSourceAccount(ls, header);

    if (creatingNewOffer &&
        (ledgerVersion >= 10 ||
         (sheep.type() == ASSET_TYPE_NATIVE && ledgerVersion > 8)))
    {
        // we need to compute maxAmountOfSheepCanSell based on the
        // updated reserve to avoid selling too many and falling
        // below the reserve when we try to create the offer later on
        if (!addNumEntries(header, sourceAccount, 1))
        {
            app.getMetrics()
                .NewMeter({"op-manage-offer", "invalid", "low reserve"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_LOW_RESERVE);
            return false;
        }
    }

    TrustLineWrapper sheepLineA;
    TrustLineWrapper wheatLineA;
    if (sheep.type() != ASSET_TYPE_NATIVE)
    {
        sheepLineA = stellar::loadTrustLine(ls, getSourceID(), sheep);
    }
    if (wheat.type() != ASSET_TYPE_NATIVE)
    {
        wheatLineA = stellar::loadTrustLine(ls, getSourceID(), wheat);
    }

    maxWheatReceive = canBuyAtMost(header, sourceAccount, wheat, wheatLineA);
    if (ledgerVersion >= 10)
    {
        int64_t availableLimit =
            (wheat.type() == ASSET_TYPE_NATIVE)
                ? getMaxAmountReceive(header, sourceAccount)
                : wheatLineA.getMaxAmountReceive(header);
        if (availableLimit < getOfferBuyingLiabilities(header, offerEntry))
        {
            app.getMetrics()
                .NewMeter({"op-manage-offer", "invalid", "line-full"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_LINE_FULL);
            return false;
        }

        int64_t availableBalance =
            (sheep.type() == ASSET_TYPE_NATIVE)
                ? getAvailableBalance(header, sourceAccount)
                : sheepLineA.getAvailableBalance(header);
        if (availableBalance < getOfferSellingLiabilities(header, offerEntry))
        {
            app.getMetrics()
                .NewMeter({"op-manage-offer", "invalid", "underfunded"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_UNDERFUNDED);
            return false;
        }

        maxSheepSend = canSellAtMost(header, sourceAccount, sheep, sheepLineA);
    }
    else
    {
        int64_t maxSheepCanSell =
            canSellAtMost(header, sourceAccount, sheep, sheepLineA);
        int64_t maxSheepBasedOnWheat;
        if (!bigDivide(maxSheepBasedOnWheat, maxWheatReceive, offer.price.d,
                       offer.price.n, ROUND_DOWN))
        {
            maxSheepBasedOnWheat = INT64_MAX;
        }

        maxSheepSend = std::min({maxSheepCanSell, maxSheepBasedOnWheat});
    }
    // amount of sheep for sale is the lesser of amount we can sell and
    // amount put in the offer
    maxSheepSend = std::min(offer.amount, maxSheepSend);
    return true;
}

// you are selling sheep for wheat
// need to check the counter offers selling wheat for sheep
// see if this is modifying an old offer
// see if this offer crosses any existing offers
bool
ManageOfferOpFrame::doApply(Application& app, AbstractLedgerState& lsOuter)
{
    LedgerState ls(lsOuter);
    if (!checkOfferValid(app.getMetrics(), ls))
    {
        return false;
    }

    Asset const& sheep = mManageOffer.selling;
    Asset const& wheat = mManageOffer.buying;

    bool creatingNewOffer = false;
    uint64_t offerID = mManageOffer.offerID;

    LedgerEntry newOffer;
    newOffer.data.type(OFFER);
    if (offerID)
    { // modifying an old offer
        auto header = ls.loadHeader();
        auto sellSheepOffer = stellar::loadOffer(ls, getSourceID(), offerID);
        if (!sellSheepOffer)
        {
            app.getMetrics()
                .NewMeter({"op-manage-offer", "invalid", "not-found"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_NOT_FOUND);
            return false;
        }

        // We are releasing the liabilites associated with this offer. This is
        // required in order to produce available balance for the offer to be
        // executed. Both trust lines must be reset since it is possible that
        // the assets are updated (including the edge case that the buying and
        // selling assets are swapped).
        if (header.current().ledgerVersion >= 10)
        {
            auto sourceAccount = loadSourceAccount(ls, header);
            TrustLineWrapper wheatLineA;
            TrustLineWrapper sheepLineA;
            releaseLiabilities(ls, header, sellSheepOffer, sourceAccount,
                               wheatLineA, sheepLineA);
        }

        // rebuild offer based off the manage offer
        auto flags = sellSheepOffer.current().data.offer().flags;
        newOffer.data.offer() = buildOffer(getSourceID(), mManageOffer, flags);
        mPassive = flags & PASSIVE_FLAG;

        // WARNING: sellSheepOffer is deleted but sourceAccount is not updated
        // to reflect the change in numSubEntries at this point. However, we
        // can't update it here since doing so would modify sourceAccount,
        // which would lead to different buckets being generated.
        sellSheepOffer.erase();
    }
    else
    { // creating a new Offer
        creatingNewOffer = true;
        newOffer.data.offer() = buildOffer(getSourceID(), mManageOffer,
                                           mPassive ? PASSIVE_FLAG : 0);
    }

    innerResult().code(MANAGE_OFFER_SUCCESS);

    if (mManageOffer.amount > 0)
    {
        Price maxWheatPrice(newOffer.data.offer().price.d,
                            newOffer.data.offer().price.n);
        int64_t maxSheepSend = 0;
        int64_t maxWheatReceive = 0;
        if (!computeOfferExchangeParameters(app, ls, newOffer, creatingNewOffer,
                                            maxSheepSend, maxWheatReceive))
        {
            return false;
        }

        if (maxWheatReceive == 0)
        {
            app.getMetrics()
                .NewMeter({"op-manage-offer", "invalid", "line-full"},
                          "operation")
                .Mark();
            innerResult().code(MANAGE_OFFER_LINE_FULL);
            return false;
        }

        int64_t sheepSent, wheatReceived;
        std::vector<ClaimOfferAtom> offerTrail;
        ConvertResult r = convertWithOffers(
            ls, sheep, maxSheepSend, sheepSent, wheat, maxWheatReceive,
            wheatReceived, false,
            [this, &newOffer, &maxWheatPrice](LedgerStateEntry const& entry) {
                auto const& o = entry.current().data.offer();
                assert(o.offerID != newOffer.data.offer().offerID);
                if ((mPassive && (o.price >= maxWheatPrice)) ||
                    (o.price > maxWheatPrice))
                {
                    return OfferFilterResult::eStop;
                }
                if (o.sellerID == getSourceID())
                {
                    // we are crossing our own offer
                    innerResult().code(MANAGE_OFFER_CROSS_SELF);
                    return OfferFilterResult::eStop;
                }
                return OfferFilterResult::eKeep;
            }, offerTrail);
        assert(sheepSent >= 0);

        bool sheepStays;
        switch (r)
        {
        case ConvertResult::eOK:
            sheepStays = false;
            break;
        case ConvertResult::ePartial:
            sheepStays = true;
            break;
        case ConvertResult::eFilterStop:
            if (innerResult().code() != MANAGE_OFFER_SUCCESS)
            {
                return false;
            }
            sheepStays = true;
            break;
        default:
            abort();
        }

        // updates the result with the offers that got taken on the way
        for (auto const& oatom : offerTrail)
        {
            innerResult().success().offersClaimed.push_back(oatom);
        }

        auto header = ls.loadHeader();
        if (wheatReceived > 0)
        {
            // it's OK to use mSourceAccount, mWheatLineA and mSheepLineA
            // here as OfferExchange won't cross offers from source account
            if (wheat.type() == ASSET_TYPE_NATIVE)
            {
                auto sourceAccount = loadSourceAccount(ls, header);
                if (!addBalance(header, sourceAccount, wheatReceived))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer claimed over limit");
                }
            }
            else
            {
                auto wheatLineA = loadTrustLine(ls, getSourceID(), wheat);
                if (!wheatLineA.addBalance(header, wheatReceived))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer claimed over limit");
                }
            }

            if (sheep.type() == ASSET_TYPE_NATIVE)
            {
                auto sourceAccount = loadSourceAccount(ls, header);
                if (!addBalance(header, sourceAccount, -sheepSent))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer sold more than balance");
                }
            }
            else
            {
                auto sheepLineA = loadTrustLine(ls, getSourceID(), sheep);
                if (!sheepLineA.addBalance(header, -sheepSent))
                {
                    // this would indicate a bug in OfferExchange
                    throw std::runtime_error("offer sold more than balance");
                }
            }
        }

        newOffer.data.offer().amount = maxSheepSend - sheepSent;
        if (header.current().ledgerVersion >= 10)
        {
            if (sheepStays)
            {
                auto sourceAccount = stellar::loadAccountWithoutRecord(ls, getSourceID());
                ConstTrustLineWrapper sheepLineA;
                ConstTrustLineWrapper wheatLineA;
                if (sheep.type() != ASSET_TYPE_NATIVE)
                {
                    sheepLineA = stellar::loadTrustLineWithoutRecord(ls, getSourceID(), sheep);
                }
                if (wheat.type() != ASSET_TYPE_NATIVE)
                {
                    wheatLineA = stellar::loadTrustLineWithoutRecord(ls, getSourceID(), wheat);
                }

                OfferEntry& oe = newOffer.data.offer();
                int64_t maxSheepSend =
                    std::min({oe.amount, canSellAtMost(header, sourceAccount, sheep, sheepLineA)});
                int64_t maxWheatReceive = canBuyAtMost(header, sourceAccount, wheat, wheatLineA);
                oe.amount = adjustOffer(oe.price, maxSheepSend, maxWheatReceive);
            }
            else
            {
                newOffer.data.offer().amount = 0;
            }
        }
    }

    auto header = ls.loadHeader();
    if (newOffer.data.offer().amount > 0)
    { // we still have sheep to sell so leave an offer
        if (creatingNewOffer)
        {
            // make sure we don't allow us to add offers when we don't have
            // the minbalance (should never happen at this stage in v9+)
            auto sourceAccount = loadSourceAccount(ls, header);
            if (!addNumEntries(header, sourceAccount, 1))
            {
                app.getMetrics()
                    .NewMeter({"op-manage-offer", "invalid", "low reserve"},
                              "operation")
                    .Mark();
                innerResult().code(MANAGE_OFFER_LOW_RESERVE);
                return false;
            }
            newOffer.data.offer().offerID = generateID(header);
            innerResult().success().offer.effect(MANAGE_OFFER_CREATED);
        }
        else
        {
            innerResult().success().offer.effect(MANAGE_OFFER_UPDATED);
        }
        auto sellSheepOffer = ls.create(newOffer);
        innerResult().success().offer.offer() =
            sellSheepOffer.current().data.offer();

        if (header.current().ledgerVersion >= 10)
        {
            auto sourceAccount = loadSourceAccount(ls, header);
            TrustLineWrapper wheatLineA;
            TrustLineWrapper sheepLineA;
            acquireLiabilities(ls, header, sellSheepOffer, sourceAccount,
                               wheatLineA, sheepLineA);
        }
    }
    else
    {
        innerResult().success().offer.effect(MANAGE_OFFER_DELETED);

        if (!creatingNewOffer)
        {
            auto sourceAccount = loadSourceAccount(ls, header);
            addNumEntries(header, sourceAccount, -1);
        }
    }

    app.getMetrics()
        .NewMeter({"op-create-offer", "success", "apply"}, "operation")
        .Mark();
    ls.commit();
    return true;
}

// makes sure the currencies are different
bool
ManageOfferOpFrame::doCheckValid(Application& app)
{
    Asset const& sheep = mManageOffer.selling;
    Asset const& wheat = mManageOffer.buying;

    if (!isAssetValid(sheep) || !isAssetValid(wheat))
    {
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "invalid-asset"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_MALFORMED);
        return false;
    }
    if (compareAsset(sheep, wheat))
    {
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "equal-currencies"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_MALFORMED);
        return false;
    }
    if (mManageOffer.amount < 0 || mManageOffer.price.d <= 0 ||
        mManageOffer.price.n <= 0)
    {
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "negative-or-zero-values"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_MALFORMED);
        return false;
    }
    if (app.getLedgerManager().getCurrentLedgerVersion() > 2 &&
        mManageOffer.offerID == 0 && mManageOffer.amount == 0)
    { // since version 3 of ledger you cannot send
        // offer operation with id and
        // amount both equal to 0
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "create-with-zero"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_NOT_FOUND);
        return false;
    }

    return true;
}

// makes sure the currencies are different
bool
ManageOfferOpFrame::doCheckValid(Application& app, uint32_t ledgerVersion)
{
    Asset const& sheep = mManageOffer.selling;
    Asset const& wheat = mManageOffer.buying;

    if (!isAssetValid(sheep) || !isAssetValid(wheat))
    {
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "invalid-asset"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_MALFORMED);
        return false;
    }
    if (compareAsset(sheep, wheat))
    {
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "equal-currencies"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_MALFORMED);
        return false;
    }
    if (mManageOffer.amount < 0 || mManageOffer.price.d <= 0 ||
        mManageOffer.price.n <= 0)
    {
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "negative-or-zero-values"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_MALFORMED);
        return false;
    }
    if (ledgerVersion > 2 &&
        mManageOffer.offerID == 0 && mManageOffer.amount == 0)
    { // since version 3 of ledger you cannot send
        // offer operation with id and
        // amount both equal to 0
        app.getMetrics()
            .NewMeter({"op-manage-offer", "invalid", "create-with-zero"},
                      "operation")
            .Mark();
        innerResult().code(MANAGE_OFFER_NOT_FOUND);
        return false;
    }

    return true;
}

OfferEntry
ManageOfferOpFrame::buildOffer(AccountID const& account,
                               ManageOfferOp const& op, uint32 flags)
{
    OfferEntry o;
    o.sellerID = account;
    o.amount = op.amount;
    o.price = op.price;
    o.offerID = op.offerID;
    o.selling = op.selling;
    o.buying = op.buying;
    o.flags = flags;
    return o;
}
}
