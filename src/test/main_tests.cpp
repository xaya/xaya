// Copyright (c) 2014-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <validation.h>
#include <net.h>

#include <test/test_bitcoin.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    const int maxHalvings = 64;
    const CAmount nInitialSubsidy = consensusParams.initialSubsidy;

    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        /* In Xaya, we have the special rule that the block reward was set
           to 1 CHI at the beginning of the first halving period (before the
           POST_ICO fork).  Thus we use the *end* of the halving period
           for the purpose of this test.  */
        nHeight += consensusParams.nSubsidyHalvingInterval - 1;

        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval, consensusParams), 0);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    TestBlockSubsidyHalvings(chainParams->GetConsensus()); // As in main
    /* Testing other intervals as is done upstream doesn't work, as we would
       need to craft a ConsensusRules instance for the POST_ICO fork check.  */
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

    CAmount nSum = 0;
    CAmount nLastSubsidy;
    int nLastHeight = 0;

    /* Updates the current state (contained in the variables this closes over)
       based on the block subsidy at the given height, assuming that it is the
       same from nLastHeight+1 up to height.  */
    const auto updateState = [&] (const int height)
      {
        const int numBlocks = height - nLastHeight;
        BOOST_CHECK(numBlocks > 0);

        const CAmount nSubsidy = GetBlockSubsidy(height, chainParams->GetConsensus());
        BOOST_CHECK(nSubsidy <= 4 * COIN);

        nSum += nSubsidy * numBlocks;
        nLastSubsidy = nSubsidy;
        BOOST_CHECK(MoneyRange(nSum));
        nLastHeight = height;
      };

    updateState(1);
    updateState(439999);
    BOOST_CHECK_EQUAL(nLastSubsidy, 1 * COIN);
    updateState(440000);
    BOOST_CHECK(nLastSubsidy > 3 * COIN);
    updateState(4199999);
    BOOST_CHECK(nLastSubsidy > 3 * COIN);
    updateState(4200000);
    BOOST_CHECK(nLastSubsidy < 2 * COIN);

    /* We need to call updateState always at the *last* block that has
       a certain subsidy.  Thus start iterating on the next block batch
       (100k more than first halving), and always update on height-1.  */
    for (int height = 4300000; height < 140000000; height += 100000)
        updateState(height - 1);

    BOOST_CHECK_EQUAL(nLastSubsidy, CAmount{0});
    /* This is the real PoW coin supply, taking rounding into account.  The
       final coin supply is larger by the presale amount.  */
    BOOST_CHECK_EQUAL(nSum, CAmount{3092157231160000});
}

BOOST_AUTO_TEST_CASE(subsidy_post_ico_fork_test)
{
    const auto main = CreateChainParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(439999, main->GetConsensus()), COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(440000, main->GetConsensus()), 382934346);

    const auto test = CreateChainParams(CBaseChainParams::TESTNET);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(10999, test->GetConsensus()), COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(11000, test->GetConsensus()), 10 * COIN);

    const auto regtest = CreateChainParams(CBaseChainParams::REGTEST);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, regtest->GetConsensus()), 50 * COIN);
}

static bool ReturnFalse() { return false; }
static bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}
BOOST_AUTO_TEST_SUITE_END()
