// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "main/Application.h"
#include "util/Timer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "crypto/Base58.h"
#include "ledger/LedgerManagerImpl.h"

#include "main/Config.h"

#include "ledger/AccountFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "transactions/TxTests.h"

using namespace stellar;
using namespace std;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("ledgerheader", "[ledger]")
{

    Config cfg(getTestConfig());

    cfg.DATABASE = "sqlite3://test.db";

    Hash saved;
    {
        cfg.REBUILD_DB = true;
        VirtualClock clock;
        Application::pointer app = Application::create(clock, cfg);
        app->start();

        TxSetFramePtr txSet = make_shared<TxSetFrame>(
            app->getLedgerManagerImpl().getLastClosedLedgerHeader().hash);

        // close this ledger
        LedgerCloseData ledgerData(1, txSet, 1, 10);
        app->getLedgerManagerImpl().closeLedger(ledgerData);

        saved = app->getLedgerManagerImpl().getLastClosedLedgerHeader().hash;
    }

    SECTION("load existing ledger")
    {
        Config cfg2(cfg);
        cfg2.REBUILD_DB = false;
        cfg2.START_NEW_NETWORK = false;
        VirtualClock clock2;
        Application::pointer app2 = Application::create(clock2, cfg2);
        app2->start();

        REQUIRE(saved ==
                app2->getLedgerManagerImpl().getLastClosedLedgerHeader().hash);
    }
}

TEST_CASE("accountCreate", "[accountstress]")
{

    Config cfg(getTestConfig(0, Config::TESTDB_TCP_LOCALHOST_POSTGRESQL));

    cfg.REBUILD_DB = true;
    VirtualClock clock;
    Application::pointer app = Application::create(clock, cfg);
    app->start();

    AccountFrame account;
    time_t start = time(NULL);
    int target = 1000000;
    const int block = 5000;
    const int txSetSize = 1000;
    int count;
    LedgerHeader h;
    for (count = 0; count < target; count++)
    {
        LedgerDelta delta(h);
        soci::transaction tx(app->getDatabase().getSession());
        int txEnd = count + txSetSize;
        for (; count < txEnd; count++)
        {
            if (count % block == 0)
            {
                time_t now = time(NULL);
                if (now != start)
                {
                    double rate = double(count) / double(now - start);
                    LOG(INFO) << "Done : " << count << " @ " << rate << " accounts/second";
                }
            }
            memcpy(&account.getAccount().accountID, &count, sizeof(count));
            uint32_t data = rand();
            memcpy(&account.getAccount().accountID[4], &data, sizeof(data));
            account.getAccount().balance = rand();
            account.storeAdd(delta, app->getDatabase());
        }
        tx.commit();
    }
    time_t done = time(NULL);
    double rate = double(count) / double(done - start);
    LOG(INFO) << "Done : " << count << " @ " << rate << " accounts/second";
}

TEST_CASE("paymentSim", "[paymentdbtest]")
{

    Config cfg(getTestConfig(0, Config::TESTDB_TCP_LOCALHOST_POSTGRESQL));

    cfg.REBUILD_DB = true;
    VirtualClock clock;
    Application::pointer app = Application::create(clock, cfg);
    app->start();

    AccountFrame account, rootAccount;
    SecretKey root = txtest::getRoot();
    REQUIRE(AccountFrame::loadAccount(root.getPublicKey(), rootAccount, app->getDatabase()));
    time_t start = time(NULL);
    int target = 1000000;
    const int block = 5000;
    const int txSetSize = 1000;
    int count;
    LedgerHeader h;
    for (count = 0; count < target; count++)
    {
        LedgerDelta delta(h);
        soci::transaction tx(app->getDatabase().getSession());
        int txEnd = count + txSetSize;
        for (; count < txEnd; count++)
        {
            if (count % block == 0)
            {
                time_t now = time(NULL);
                if (now != start)
                {
                    double rate = double(count) / double(now - start);
                    LOG(INFO) << "Done : " << count << " @ " << rate << " accounts/second";
                }
            }
            memcpy(&account.getAccount().accountID, &count, sizeof(count));
            uint32_t data = rand();
            memcpy(&account.getAccount().accountID[4], &data, sizeof(data));
            account.getAccount().balance = rand();
            account.storeAdd(delta, app->getDatabase());
            rootAccount.getAccount().balance -= rand();
            rootAccount.storeChange(delta, app->getDatabase());
        }
        tx.commit();
    }
    time_t done = time(NULL);
    double rate = double(count) / double(done - start);
    LOG(INFO) << "Done : " << count << " @ " << rate << " accounts/second";
}
