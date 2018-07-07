// Copyright (c) 2018 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <pow.h>
#include <powdata.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

/* No space between BOOST_FIXTURE_TEST_SUITE and '(', so that extraction of
   the test-suite name works with grep as done in the Makefile.  */
BOOST_FIXTURE_TEST_SUITE(dualalgo_tests, TestingSetup)

/* ************************************************************************** */

namespace
{

/* Original DGW difficulty formula that does not take algo into account except
   for the PoW limit.  We use that as golden truth after separating the blocks
   of each difficulty into individual chains to test our per-algo difficulty
   retargeting function.  */
unsigned int
OriginalDGW(const PowAlgo algo, const CBlockIndex* pindexLast, const Consensus::Params& params) {
    /* current difficulty formula, dash - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    const arith_uint256 bnPowLimit
        = UintToArith256(powLimitForAlgo(algo, params));
    constexpr int64_t nPastBlocks = 24;

    // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return powLimit
    if (!pindexLast || pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex *pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        const arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if(nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

class TestChain
{

private:

  std::vector<std::unique_ptr<CBlockIndex>> blocks;

public:

  CBlockIndex*
  tip () const
  {
    if (blocks.empty ())
      return nullptr;
    return blocks.back ().get ();
  }

  void
  attach (const CBlockIndex& indexNew)
  {
    std::unique_ptr<CBlockIndex> modified(new CBlockIndex (indexNew));
    modified->pprev = tip ();
    modified->nHeight = blocks.size ();
    blocks.push_back (std::move (modified));
  }

};

} // anonymous namespace

BOOST_AUTO_TEST_CASE (difficulty_retargeting)
{
  /* The test for our dual-algo difficulty retargeting works as follows:
     We construct a chain of blocks with randomly chosen algorithms and
     block times (nBits of each block is set according to GetNextWorkRequired,
     although that does not really matter).  We also attach the same blocks
     to two separate chains that contain only blocks of each algo.  We then
     verify that our GetNextWorkRequired function returns the same result as
     the original DGW function does for the single-algo chain.  */

  const Consensus::Params& params = Params ().GetConsensus ();
  BOOST_CHECK (!params.fPowNoRetargeting);

  for (unsigned trials = 0; trials < 5; ++trials)
    {
      TestChain mixedChain;
      std::map<PowAlgo, TestChain> perAlgoChains;

      uint32_t lastTime = 1000000000;

      for (unsigned len = 0; len < 500; ++len)
        {
          for (const PowAlgo algo : {PowAlgo::SHA256D, PowAlgo::NEOSCRYPT})
            {
              const uint32_t nextWork
                  = GetNextWorkRequired (algo, mixedChain.tip (), params);
              const uint32_t golden
                  = OriginalDGW (algo, perAlgoChains[algo].tip (), params);
              BOOST_CHECK_EQUAL (nextWork, golden);
            }

          CBlockIndex indexNew;
          if (InsecureRandBool ())
            indexNew.algo = PowAlgo::SHA256D;
          else
            indexNew.algo = PowAlgo::NEOSCRYPT;
          indexNew.nBits = GetNextWorkRequired (indexNew.algo,
                                                mixedChain.tip (), params);

          /* Increment the time typically, but allow also decrements.  Average
             time increase should be nPowTargetSpacing/2 since we have two
             algos, so choosing randomly in [-10, target spacing - 10] seems
             like a good enough approximation.  Since these values are actually
             then a bit faster than expected, we increase the difficulty away
             from the minimum, which is also good for the test.  */
          indexNew.nTime = lastTime
                            + InsecureRandRange (params.nPowTargetSpacing)
                            - 10;
          lastTime = indexNew.nTime;

          mixedChain.attach (indexNew);
          perAlgoChains[indexNew.algo].attach (indexNew);
        }

      /* Verify that we have actually increased the difficulty and not just
         stuck to the minimum (which would make the test somewhat trivial).  */
      for (const auto& entry : perAlgoChains)
        BOOST_CHECK (entry.second.tip ()->nBits
                      != GetNextWorkRequired (entry.first, nullptr, params));
    }
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
