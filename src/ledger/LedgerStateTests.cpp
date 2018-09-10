// Copyright 2018 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerState.h"
#include "ledger/LedgerStateEntry.h"
#include "ledger/LedgerStateHeader.h"
#include "ledger/LedgerTestUtils.h"
#include "lib/catch.hpp"
#include "main/Application.h"
#include "test/TestUtils.h"
#include "test/test.h"
#include "util/XDROperators.h"
#include <map>
#include <memory>
#include <queue>
#include <set>

using namespace stellar;

void validate(
    LedgerStateRoot& root,
    LedgerHeader const& expectedHeader,
    std::vector<LedgerEntry> const& expectedLive)
{
    auto count = root.countObjects(ACCOUNT) + root.countObjects(TRUSTLINE) +
                 root.countObjects(OFFER) + root.countObjects(DATA);
    REQUIRE(count - 1 == expectedLive.size()); // Ignore root account

    LedgerState ls(root);
    REQUIRE(ls.loadHeader().current() == expectedHeader);
    for (auto const& entry : expectedLive)
    {
        auto loaded = ls.load(LedgerEntryKey(entry));
        REQUIRE(loaded);
        REQUIRE(loaded.current() == entry);
    }
}

TEST_CASE("LedgerState create", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    LedgerEntry le = LedgerTestUtils::generateValidLedgerEntry();
    LedgerKey key = LedgerEntryKey(le);

    SECTION("key does not exist")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            ls.commit();
        }
        validate(root, lh, {le});
    }

    SECTION("key exists but is not active")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            REQUIRE_THROWS_AS(ls.create(le), std::runtime_error);
            ls.commit();
        }
        validate(root, lh, {le});
    }

    SECTION("key exists and is active")
    {
        {
            LedgerState ls(root, false);
            auto entry = ls.create(le);
            REQUIRE_THROWS_AS(ls.create(le), std::runtime_error);
            entry.deactivate();
            ls.commit();
        }
        validate(root, lh, {le});
    }
}

TEST_CASE("LedgerState create then erase", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    LedgerEntry le = LedgerTestUtils::generateValidLedgerEntry();
    LedgerKey key = LedgerEntryKey(le);

    SECTION("same LedgerState")
    {
        {
            LedgerState ls(root, false);
            ls.create(le).erase();
            ls.commit();
        }
        validate(root, lh, {});
    }

    SECTION("nested LedgerState")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            REQUIRE(ls.load(key).current() == le);
            {
                LedgerState ls2(ls, false);
                ls2.erase(key);
                REQUIRE(!ls2.load(key));
                ls2.commit();
            }
            ls.commit();
        }
        validate(root, lh, {});
    }
}

TEST_CASE("LedgerState load header then modify", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();
    auto lh2 = lh;
    ++lh2.ledgerSeq;

    {
        LedgerState ls(root, false);
        REQUIRE(ls.loadHeader().current() == lh);
        {
            LedgerState ls2(ls, false);
            ls2.loadHeader().current() = lh2;
            ls2.commit();
        }
        ls.commit();
    }
    validate(root, lh2, {});
}

TEST_CASE("LedgerState load mutually exclusive with loadWithoutRecord",
          "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    LedgerEntry le = LedgerTestUtils::generateValidLedgerEntry();
    LedgerKey key = LedgerEntryKey(le);

    SECTION("load then load")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            auto entry = ls.load(key);
            REQUIRE(entry.current() == le);
            REQUIRE_THROWS_AS(ls.load(key), std::runtime_error);
            entry.deactivate();
            ls.commit();
        }
        validate(root, lh, {le});
    }

    SECTION("load then loadWithoutRecord")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            auto entry = ls.load(key);
            REQUIRE(entry.current() == le);
            REQUIRE_THROWS_AS(ls.loadWithoutRecord(key), std::runtime_error);
            entry.deactivate();
            ls.commit();
        }
        validate(root, lh, {le});
    }

    SECTION("loadWithoutRecord then load")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            auto entry = ls.loadWithoutRecord(key);
            REQUIRE(entry.current() == le);
            REQUIRE_THROWS_AS(ls.load(key), std::runtime_error);
            entry.deactivate();
            ls.commit();
        }
        validate(root, lh, {le});
    }

    SECTION("loadWithoutRecord then loadWithoutRecord")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            auto entry = ls.loadWithoutRecord(key);
            REQUIRE(entry.current() == le);
            REQUIRE_THROWS_AS(ls.loadWithoutRecord(key), std::runtime_error);
            entry.deactivate();
            ls.commit();
        }
        validate(root, lh, {le});
    }
}

TEST_CASE("LedgerState rollback deactivates", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    LedgerEntry le = LedgerTestUtils::generateValidLedgerEntry();
    LedgerKey key = LedgerEntryKey(le);

    SECTION("entry")
    {
        {
            LedgerState ls(root, false);
            auto entry = ls.create(le);
            REQUIRE(entry);
            ls.rollback();
            REQUIRE(!entry);
        }
        validate(root, lh, {});
    }

    SECTION("const entry")
    {
        {
            LedgerState ls(root, false);
            ls.create(le);
            auto entry = ls.loadWithoutRecord(key);
            REQUIRE(entry);
            ls.rollback();
            REQUIRE(!entry);
        }
        validate(root, lh, {});
    }

    SECTION("header")
    {
        {
            LedgerState ls(root, false);
            auto header = ls.loadHeader();
            REQUIRE(header);
            ls.rollback();
            REQUIRE(!header);
        }
        validate(root, lh, {});
    }
}

TEST_CASE("LedgerStateRoot round trip", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    auto check = [&](LedgerEntry const& le) {
        LedgerKey key = LedgerEntryKey(le);
        {
            LedgerState ls(root, false);
            ls.create(le);
            ls.commit();
        }
        {
            LedgerState ls(root, false);
            REQUIRE(ls.load(key).current() == le);
        }
    };

    SECTION("account")
    {
        LedgerEntry le;
        le.data.type(ACCOUNT);
        le.data.account() = LedgerTestUtils::generateValidAccountEntry();
        check(le);
    }

    SECTION("data")
    {
        LedgerEntry le;
        le.data.type(DATA);
        le.data.data() = LedgerTestUtils::generateValidDataEntry();
        LedgerKey key = LedgerEntryKey(le);
        check(le);
    }

    SECTION("offer")
    {
        LedgerEntry le;
        le.data.type(OFFER);
        le.data.offer() = LedgerTestUtils::generateValidOfferEntry();
        LedgerKey key = LedgerEntryKey(le);
        check(le);
    }

    SECTION("trustline")
    {
        LedgerEntry le;
        le.data.type(TRUSTLINE);
        le.data.trustLine() = LedgerTestUtils::generateValidTrustLineEntry();
        LedgerKey key = LedgerEntryKey(le);
        check(le);
    }
}

TEST_CASE("Move assignment", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    LedgerEntry le1 = LedgerTestUtils::generateValidLedgerEntry();
    LedgerKey key1 = LedgerEntryKey(le1);
    LedgerEntry le2 = LedgerTestUtils::generateValidLedgerEntry();
    LedgerKey key2 = LedgerEntryKey(le2);

    SECTION("assign self")
    {
        SECTION("entry")
        {
            {
                LedgerState ls(root, false);
                auto entry1 = ls.create(le1);
                // Avoid warning for explicit move-to-self
                LedgerStateEntry& entryRef = entry1;
                entry1 = std::move(entryRef);
                REQUIRE(entry1.current() == le1);
                REQUIRE_THROWS_AS(ls.load(key1), std::runtime_error);
                REQUIRE_THROWS_AS(ls.loadWithoutRecord(key1),
                                  std::runtime_error);
            }
            validate(root, lh, {});
        }

        SECTION("const entry")
        {
            {
                LedgerState ls(root, false);
                ls.create(le1);
                auto entry1 = ls.loadWithoutRecord(key1);
                // Avoid warning for explicit move-to-self
                ConstLedgerStateEntry& entryRef = entry1;
                entry1 = std::move(entryRef);
                REQUIRE(entry1.current() == le1);
                REQUIRE_THROWS_AS(ls.load(key1), std::runtime_error);
                REQUIRE_THROWS_AS(ls.loadWithoutRecord(key1),
                                  std::runtime_error);
            }
            validate(root, lh, {});
        }

        SECTION("header")
        {
            {
                LedgerState ls(root, false);
                auto header = ls.loadHeader();
                // Avoid warning for explicit move-to-self
                LedgerStateHeader& headerRef = header;
                header = std::move(headerRef);
                REQUIRE(header.current() == lh);
                REQUIRE_THROWS_AS(ls.loadHeader(), std::runtime_error);
            }
            validate(root, lh, {});
        }
    }

    SECTION("assign other")
    {
        SECTION("entry")
        {
            {
                LedgerState ls(root, false);
                auto entry1 = ls.create(le1);
                auto entry2 = ls.create(le2);
                entry1 = std::move(entry2);
                REQUIRE(entry1.current() == le2);
                REQUIRE_THROWS_AS(ls.load(key2), std::runtime_error);
                REQUIRE(ls.load(key1).current() == le1);
                REQUIRE(ls.loadWithoutRecord(key1).current() == le1);
            }
            validate(root, lh, {});
        }

        SECTION("const entry")
        {
            {
                LedgerState ls(root, false);
                ls.create(le1);
                ls.create(le2);
                auto entry1 = ls.loadWithoutRecord(key1);
                auto entry2 = ls.loadWithoutRecord(key2);
                entry1 = std::move(entry2);
                REQUIRE(entry1.current() == le2);
                REQUIRE_THROWS_AS(ls.load(key2), std::runtime_error);
                REQUIRE(ls.load(key1).current() == le1);
                REQUIRE(ls.loadWithoutRecord(key1).current() == le1);
            }
            validate(root, lh, {});
        }

        SECTION("header")
        {
            {
                LedgerState ls(root, false);
                auto header1 = ls.loadHeader();
                LedgerStateHeader header2 = std::move(header1);
                REQUIRE(header2.current() == lh);
                REQUIRE_THROWS_AS(ls.loadHeader(), std::runtime_error);
            }
            validate(root, lh, {});
        }
    }
}

TEST_CASE("LedgerState loadOffersByAccountAndAsset", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    LedgerEntry ae;
    ae.data.type(ACCOUNT);
    ae.data.account() = LedgerTestUtils::generateValidAccountEntry();
    LedgerKey aeKey = LedgerEntryKey(ae);

    LedgerEntry oe;
    oe.data.type(OFFER);
    oe.data.offer() = LedgerTestUtils::generateValidOfferEntry();
    oe.data.offer().sellerID = ae.data.account().accountID;
    LedgerKey oeKey = LedgerEntryKey(oe);

    {
        LedgerState ls(root, false);
        ls.create(ae);
        ls.create(oe);
        ls.commit();
    }
    {
        LedgerState ls(root, false);
        {
            auto entries =
                ls.loadOffersByAccountAndAsset(ae.data.account().accountID,
                                               oe.data.offer().buying);
            REQUIRE(entries.size() == 1);
            REQUIRE(entries[0].current() == oe);
        }
        {
            auto entries =
                ls.loadOffersByAccountAndAsset(ae.data.account().accountID,
                                               oe.data.offer().selling);
            REQUIRE(entries.size() == 1);
            REQUIRE(entries[0].current() == oe);
        }

        ls.erase(oeKey);
        {
            auto entries =
                ls.loadOffersByAccountAndAsset(ae.data.account().accountID,
                                               oe.data.offer().buying);
            REQUIRE(entries.empty());
        }
        {
            auto entries =
                ls.loadOffersByAccountAndAsset(ae.data.account().accountID,
                                               oe.data.offer().selling);
            REQUIRE(entries.empty());
        }
    }
}

TEST_CASE("LedgerState getInflationWinners", "[ledgerstate]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto& root = app->getLedgerStateRoot();
    auto lh = root.getHeader();

    LedgerEntry acc;
    acc.data.type(ACCOUNT);
    acc.data.account() = LedgerTestUtils::generateValidAccountEntry();
    acc.data.account().inflationDest.reset();

    LedgerEntry voter;
    voter.data.type(ACCOUNT);
    voter.data.account() = LedgerTestUtils::generateValidAccountEntry();
    voter.data.account().inflationDest.activate() = acc.data.account().accountID;

    int64_t const QUERY_VOTE_MINIMUM = 1000000000;

    SECTION("new voter")
    {
        auto voterCopy = voter;
        voterCopy.data.account().balance = 2*QUERY_VOTE_MINIMUM;

        LedgerState ls(app->getLedgerStateRoot());
        ls.create(voterCopy);
        SECTION("with enough balance to meet minimum votes")
        {
            auto winners = ls.getInflationWinners(1, 2*QUERY_VOTE_MINIMUM);
            REQUIRE(winners.size() == 1);
            REQUIRE(winners[0].accountID == acc.data.account().accountID);
        }
        SECTION("without enough balance to meet minimum votes")
        {
            auto winners = ls.getInflationWinners(1, 2*QUERY_VOTE_MINIMUM + 1);
            REQUIRE(winners.size() == 0);
        }
    }

    SECTION("changed voter")
    {
        auto voter1 = voter;
        voter1.data.account().balance = 2*QUERY_VOTE_MINIMUM;
        auto voter2 = voter;
        voter2.data.account().balance = 2*QUERY_VOTE_MINIMUM - 1;

        LedgerState ls1(app->getLedgerStateRoot());
        SECTION("with enough balance to meet minimum votes")
        {
            ls1.create(voter2);
            LedgerState ls2(ls1);
            ls2.load(LedgerEntryKey(voter)).current() = voter1;
            auto winners = ls2.getInflationWinners(1, 2*QUERY_VOTE_MINIMUM);
            REQUIRE(winners.size() == 1);
            REQUIRE(winners[0].accountID == acc.data.account().accountID);
        }
        SECTION("without enough balance to meet minimum votes")
        {
            ls1.create(voter1);
            LedgerState ls2(ls1);
            ls2.load(LedgerEntryKey(voter)).current() = voter2;
            auto winners = ls2.getInflationWinners(1, 2*QUERY_VOTE_MINIMUM);
            REQUIRE(winners.size() == 0);
        }
    }
}
