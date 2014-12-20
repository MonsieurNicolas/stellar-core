#ifndef __DECIMAL__
#define __DECIMAL__

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "generated/StellarXDR.h"

extern "C"
{
#define DECEXTFLAG 0
#include <decQuad.h>
}

/**
 * C++ wrapper around IBM decQuad type implementing the decimal128 type from
 * IEEE754-2008 and ISO/IEC/IEEE 60559:2011.
 *
 * No extra padding, just a little extra typesafety and automatic checking of
 * flags, converting to exceptions and canonicalizing results of operations.
 *
 * Note: a decQuad is just a plain 16 byte array holding DPD values:
 *
 *    uint8_t   bytes[DECQUAD_Bytes]
 *
 * This fits in 2 GPRs or a single XMM register (of which there are 16) on x64.
 * Don't do anything clever to "optimize for memory", just treat them as
 * scalars.
 */
class DecContext;

class DecQuad
{
    decQuad mQuad;
    friend class DecContext;
public:
    DecQuad()
    {
        decQuadZero(&mQuad);
    }
    DecQuad(DecQuad const& other)
    {
        decQuadCopy(&mQuad, &other.mQuad);
    }
    DecQuad(uint32_t u)
    {
        decQuadFromUInt32(&mQuad, u);
    }
    DecQuad(stellarxdr::decimal128 const& d)
    {
        memcpy(&mQuad.bytes, d.data(), DECQUAD_Bytes);
    }
    DecQuad const& operator=(DecQuad const& other)
    {
        decQuadCopy(&mQuad, &other.mQuad);
        return *this;
    }
    void canonicalize()
    {
        decQuadCanonical(&mQuad, &mQuad);
    }
    bool isPositive()
    {
        return decQuadIsPositive(&mQuad) != 0;
    }
    bool isNegative()
    {
        return decQuadIsNegative(&mQuad) != 0;
    }
    bool isFinite()
    {
        return decQuadIsFinite(&mQuad) != 0;
    }
    bool isInteger()
    {
        return decQuadIsInteger(&mQuad) != 0;
    }
    bool isZero()
    {
        return decQuadIsZero(&mQuad) != 0;
    }
    std::string toString()
    {
        char buf[DECQUAD_String];
        decQuadToString(&mQuad, buf);
        return std::string(buf);
    }
    stellarxdr::decimal128 toXDR()
    {
        stellarxdr::decimal128 res;
        memcpy(res.data(), &mQuad.bytes, DECQUAD_Bytes);
        return res;
    }
};


class DecContext
{
    decContext mContext;
public:
    DecContext()
    {
        decContextDefault(&mContext, DEC_INIT_DECIMAL128);
    }
    void checkStatus()
    {
        uint32_t const bad =
            DEC_IEEE_754_Division_by_zero |
            DEC_IEEE_754_Invalid_operation |
            DEC_IEEE_754_Overflow |
            DEC_IEEE_754_Underflow |
            DEC_IEEE_754_Inexact;

        if ((mContext.status & bad) != 0)
        {
            throw std::range_error(decContextStatusToString(&mContext));
        }
    }

    DecQuad abs(DecQuad const& a)
    {
        DecQuad res;
        decQuadAbs(&res.mQuad, &a.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad add(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadAdd(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad sub(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadSubtract(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad mul(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadMultiply(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad div(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadDivide(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad rem(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadRemainder(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad min(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadMin(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad max(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadMax(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad invert(DecQuad const& a)
    {
        DecQuad res;
        decQuadInvert(&res.mQuad, &a.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    DecQuad negate(DecQuad const& a)
    {
        DecQuad res;
        decQuadMinus(&res.mQuad, &a.mQuad, &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }

    int compare(DecQuad const& a, DecQuad const& b)
    {
        DecQuad res;
        decQuadCompare(&res.mQuad, &a.mQuad, &b.mQuad, &mContext);
        checkStatus();
        if (res.isZero())
            return 0;
        if (res.isNegative())
            return -1;
        if (res.isPositive())
            return 1;
        throw std::range_error("unordered DecQuad comparison");
    }

    bool isLessThan(DecQuad const& a, DecQuad const& b)
    {
        return compare(a, b) < 0;
    }

    DecQuad fromString(std::string const& s)
    {
        DecQuad res;
        decQuadFromString(&res.mQuad, s.c_str(), &mContext);
        checkStatus();
        res.canonicalize();
        return res;
    }
};

#endif
