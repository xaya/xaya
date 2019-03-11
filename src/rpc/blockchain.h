// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_BLOCKCHAIN_H
#define BITCOIN_RPC_BLOCKCHAIN_H

#include <vector>
#include <stdint.h>
#include <amount.h>

class CBlock;
class CBlockIndex;
class CTxMemPool;
class UniValue;
class JSONRPCRequest;

static constexpr int NUM_GETBLOCKSTATS_PERCENTILES = 5;

/**
 * Returns the numeric difficulty for the given nBits.
 */
double GetDifficultyForBits(uint32_t nBits);

/** Callback for when block tip changed. */
void RPCNotifyBlockChange(bool ibd, const CBlockIndex *);

/** Block description to JSON */
UniValue blockToJSON(const CBlock& block, const CBlockIndex* tip, const CBlockIndex* blockindex, bool txDetails = false);

/** Mempool information to JSON */
UniValue MempoolInfoToJSON(const CTxMemPool& pool);

/** Mempool to JSON */
UniValue MempoolToJSON(const CTxMemPool& pool, bool verbose = false);

/** Block header to JSON */
UniValue blockheaderToJSON(const CBlockIndex* tip, const CBlockIndex* blockindex);

/** Used by getblockstats to get feerates at different percentiles by weight  */
void CalculatePercentilesByWeight(CAmount result[NUM_GETBLOCKSTATS_PERCENTILES], std::vector<std::pair<CAmount, int64_t>>& scores, int64_t total_weight);

UniValue getdifficulty(const JSONRPCRequest& request);

#endif
