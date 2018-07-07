// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/feerate.h>

#include <tinyformat.h>

const std::string CURRENCY_UNIT = "CHI";

CFeeRate::CFeeRate(const CAmount& nFeePaid, size_t nBytes_)
{
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    if (nSize > 0) {
        /* Xaya's MAX_MONEY is so large that 1000 * MAX_MONEY overflows
           int64_t (CAmount).  Thus we need special-casing here for the
           very unlikely (except in the unit test) case of an insanely high
           fee paid.  */
        if (nFeePaid > 1000000 * COIN) {
            if (nFeePaid / nSize > MAX_MONEY / 1000) {
                nSatoshisPerK = MAX_MONEY;
            } else {
                nSatoshisPerK = (nFeePaid / nSize) * 1000;
            }
        } else {
            nSatoshisPerK = nFeePaid * 1000 / nSize;
        }
    } else
        nSatoshisPerK = 0;
}

CAmount CFeeRate::GetFee(size_t nBytes_) const
{
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    CAmount nFee = nSatoshisPerK * nSize / 1000;

    if (nFee == 0 && nSize != 0) {
        if (nSatoshisPerK > 0)
            nFee = CAmount(1);
        if (nSatoshisPerK < 0)
            nFee = CAmount(-1);
    }

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%08d %s/kB", nSatoshisPerK / COIN, nSatoshisPerK % COIN, CURRENCY_UNIT);
}
