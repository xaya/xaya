// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <powdata.h>
#include <uint256.h>

unsigned int
GetNextWorkRequired (const PowAlgo algo, const CBlockIndex* pindexLast,
                     const Consensus::Params& params)
{
  const arith_uint256 bnPowLimit
      = UintToArith256 (powLimitForAlgo (algo, params));

  if (pindexLast == nullptr || params.fPowNoRetargeting)
      return bnPowLimit.GetCompact ();

  /* The actual difficulty update below is DGW taken from Dash, except that
     we have to look at blocks of only one algo explicitly.  */

  constexpr int64_t nPastBlocks = 24;

  pindexLast = pindexLast->GetLastAncestorWithAlgo (algo);
  if (pindexLast == nullptr)
    return bnPowLimit.GetCompact ();

  const CBlockIndex* pindex = pindexLast;
  const CBlockIndex* pindexFirst = nullptr;
  arith_uint256 bnResult;
  for (size_t nCountBlocks = 1; nCountBlocks <= nPastBlocks; ++nCountBlocks)
    {
      assert (pindex != nullptr && pindex->algo == algo);

      arith_uint256 bnTarget;
      bnTarget.SetCompact (pindex->nBits);

      if (nCountBlocks == 1)
        bnResult = bnTarget;
      else
        bnResult = (bnResult * nCountBlocks + bnTarget) / (nCountBlocks + 1);

      if (nCountBlocks == nPastBlocks)
        pindexFirst = pindex;

      /* We need to step back to the last block with the given algo, but at
         least one block.  Note that GetLastAncestorWithAlgo returns the
         block itself if it matches.  */
      pindex = pindex->pprev;
      if (pindex != nullptr)
        pindex = pindex->GetLastAncestorWithAlgo (algo);

      if (pindex == nullptr)
        return bnPowLimit.GetCompact ();
    }

  int64_t nActualTimespan = pindexLast->GetBlockTime ()
                              - pindexFirst->GetBlockTime ();
  const int nextHeight = pindexLast->nHeight + 1;
  int64_t nTargetTimespan
      = nPastBlocks * Ticks<std::chrono::seconds>(params.rules->GetTargetSpacing(algo, nextHeight));

  if (nActualTimespan < nTargetTimespan / 3)
    nActualTimespan = nTargetTimespan / 3;
  if (nActualTimespan > nTargetTimespan * 3)
    nActualTimespan = nTargetTimespan * 3;

  bnResult *= nActualTimespan;
  bnResult /= nTargetTimespan;

  if (bnResult > bnPowLimit)
    bnResult = bnPowLimit;

  return bnResult.GetCompact ();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    /* TODO: Implement something proper for Xaya.  */
    return true;
}
