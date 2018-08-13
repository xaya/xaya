// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;

enum class PowAlgo : uint8_t;

unsigned int GetNextWorkRequired(PowAlgo algo, const CBlockIndex* pindexLast, const Consensus::Params&);

#endif // BITCOIN_POW_H
