// Copyright (c) 2018-2023 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <pow.h>
#include <powdata.h>

#include <test/util/random.h>
#include <test/util/setup_common.h>

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

    const int nextHeight = pindexLast->nHeight + 1;
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan
        = nPastBlocks * Ticks<std::chrono::seconds>(params.rules->GetTargetSpacing(algo, nextHeight));

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

class CopyableBlockIndex : public CBlockIndex
{

public:

  CopyableBlockIndex (const CBlockIndex& blk)
    : CBlockIndex(blk)
  {}

};

class TestChain
{

private:

  std::vector<std::unique_ptr<CopyableBlockIndex>> blocks;

public:

  CBlockIndex*
  tip () const
  {
    if (blocks.empty ())
      return nullptr;
    return blocks.back ().get ();
  }

  unsigned
  height () const
  {
    const CBlockIndex* pindex = tip ();
    return pindex == nullptr ? 0 : pindex->nHeight;
  }

  void
  attach (const CBlockIndex& indexNew)
  {
    std::unique_ptr<CopyableBlockIndex> modified(
        new CopyableBlockIndex (indexNew));
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
             time increase should be AvgTargetSpacing,
             so choosing randomly in [-10, target spacing - 10] seems
             like a good enough approximation.  Since these values are actually
             then a bit faster than expected, we increase the difficulty away
             from the minimum, which is also good for the test.  */
          const int64_t targetSpacing
              = Ticks<std::chrono::seconds>(AvgTargetSpacing (params, mixedChain.height () + 1));
          indexNew.nTime = lastTime
                            + InsecureRandRange (targetSpacing)
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

namespace
{

constexpr unsigned BEFORE_FORK = 200;
constexpr unsigned AFTER_FORK = 800;

class PostIcoForkSetup : public TestingSetup
{
public:

  const Consensus::Params* params = nullptr;

  PostIcoForkSetup ()
  {
    SelectParams (ChainType::REGTEST);
    params = &Params ().GetConsensus ();

    BOOST_CHECK (!params->rules->ForkInEffect (Consensus::Fork::POST_ICO,
                                               BEFORE_FORK));
    BOOST_CHECK (params->rules->ForkInEffect (Consensus::Fork::POST_ICO,
                                              AFTER_FORK));
  }

};

} // anonymous namespace

BOOST_FIXTURE_TEST_CASE (avg_target_spacing, PostIcoForkSetup)
{
  BOOST_CHECK_EQUAL (
      Ticks<std::chrono::seconds>(AvgTargetSpacing (*params, BEFORE_FORK)), 30);
  BOOST_CHECK_EQUAL (
      Ticks<std::chrono::seconds>(AvgTargetSpacing (*params, AFTER_FORK)), 30);
}

namespace
{

class BlockProofEquivalentTimeSetup : public PostIcoForkSetup
{
public:

  TestChain chain;

  BlockProofEquivalentTimeSetup ()
  {
    /* Turn off "no retargeting" rule.  We won't need to mine valid blocks
       anyway, and this messes with the "relative difficulty" between
       the algorithms as it disables rescaling of the minimum difficulty.  */
    const_cast<Consensus::Params*> (params)->fPowNoRetargeting = false;
  }

  /**
   * Attaches a block with minimum difficulty and the given algorithm
   * to the test chain.
   */
  void
  AttachBlock (const PowAlgo algo)
  {
    CBlockIndex indexNew;
    indexNew.algo = algo;

    const uint256 bnPowLimit = powLimitForAlgo (indexNew.algo, *params);
    indexNew.nBits = UintToArith256 (bnPowLimit).GetCompact ();

    arith_uint256 previousWork;
    if (chain.tip () != nullptr)
      previousWork = chain.tip ()->nChainWork;
    indexNew.nChainWork = previousWork + GetBlockProof (indexNew);

    chain.attach (indexNew);
  }

  /**
   * Computes the proof-equivalent-time from tip-n to tip.
   */
  int64_t
  GetEquivalentTime (const unsigned n) const
  {
    const CBlockIndex* beforeTip = chain.tip ();
    for (unsigned i = 0; i < n; ++i)
      beforeTip = beforeTip->pprev;

    return GetBlockProofEquivalentTime (*chain.tip (), *beforeTip,
                                        *chain.tip (), *params);
  }

};

} // anonymous namespace

BOOST_FIXTURE_TEST_CASE (eqv_time_before_fork, BlockProofEquivalentTimeSetup)
{
  for (unsigned i = 0; i <= BEFORE_FORK; ++i)
    AttachBlock (i % 2 == 0 ? PowAlgo::SHA256D : PowAlgo::NEOSCRYPT);
  BOOST_CHECK_EQUAL (chain.height (), BEFORE_FORK);

  BOOST_CHECK_EQUAL (GetEquivalentTime (2), 60);
}

BOOST_FIXTURE_TEST_CASE (eqv_time_after_fork, BlockProofEquivalentTimeSetup)
{
  /* After the fork, attach one SHA-256d block for every three Neoscrypt
     blocks, as that corresponds to the ratio afterwards.  We expect four
     of those blocks to be equivalent to two minutes' time.  */

  for (unsigned i = 0; i <= AFTER_FORK; ++i)
    AttachBlock (i % 4 == 0 ? PowAlgo::SHA256D : PowAlgo::NEOSCRYPT);
  BOOST_CHECK_EQUAL (chain.height (), AFTER_FORK);

  BOOST_CHECK_EQUAL (GetEquivalentTime (4), 120);
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
