// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "util/Decimal.h"
#include "lib/catch.hpp"
#include "util/Logging.h"

#include <xdrpp/autocheck.h>

using namespace stellar;

TEST_CASE("Decimal misc smoketest", "[decimal]")
{
    DecContext ctx;
    DecQuad a = ctx.fromString("100.5");
    DecQuad b(23);
    DecQuad c = ctx.add(a, b);
    LOG(DEBUG) << "Decimal: "
               << a.toString() << " + "
               << b.toString() << " = "
               << c.toString();
    CHECK(c.toString() == "123.5");

    for (int i = 0; i < 4; ++i)
    {
        c = ctx.div(c, DecQuad(2));
        LOG(DEBUG) << "Decimal: /= 2 --> " << c.toString();
        c = ctx.mul(c, DecQuad(5));
        LOG(DEBUG) << "Decimal: *= 5 --> " << c.toString();
    }
}

TEST_CASE("Decimal rounding throws", "[decimal]")
{
    DecContext ctx;
    DecQuad a = ctx.fromString("100.5");
    CHECK(ctx.div(a, DecQuad(3)).toString() == "33.5");

    a = ctx.fromString("123.5");
    CHECK_THROWS(ctx.div(a, DecQuad(3)));
}
