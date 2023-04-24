// Copyright (c) 2014-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/amount.h>
#include <net.h>
#include <signet.h>
#include <uint256.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

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
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    TestBlockSubsidyHalvings(chainParams->GetConsensus()); // As in main
    /* Testing other intervals as is done upstream doesn't work, as we would
       need to craft a ConsensusRules instance for the POST_ICO fork check.  */
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
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
    const auto main = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(439999, main->GetConsensus()), COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(440000, main->GetConsensus()), 382934346);

    const auto test = CreateChainParams(*m_node.args, CBaseChainParams::TESTNET);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(10999, test->GetConsensus()), COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(11000, test->GetConsensus()), 10 * COIN);

    const auto regtest = CreateChainParams(*m_node.args, CBaseChainParams::REGTEST);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, regtest->GetConsensus()), 50 * COIN);
}

BOOST_AUTO_TEST_CASE(signet_parse_tests)
{
    ArgsManager signet_argsman;
    signet_argsman.ForceSetArg("-signetchallenge", "51"); // set challenge to OP_TRUE
    const auto signet_params = CreateChainParams(signet_argsman, CBaseChainParams::SIGNET);
    CBlock block;
    BOOST_CHECK(signet_params->GetConsensus().signet_challenge == std::vector<uint8_t>{OP_TRUE});
    CScript challenge{OP_TRUE};

    // empty block is invalid
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no witness commitment
    CMutableTransaction cb;
    cb.vout.emplace_back(0, CScript{});
    block.vtx.push_back(MakeTransactionRef(cb));
    block.vtx.push_back(MakeTransactionRef(cb)); // Add dummy tx to exercise merkle root code
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no header is treated valid
    std::vector<uint8_t> witness_commitment_section_141{0xaa, 0x21, 0xa9, 0xed};
    for (int i = 0; i < 32; ++i) {
        witness_commitment_section_141.push_back(0xff);
    }
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // no data after header, valid
    std::vector<uint8_t> witness_commitment_section_325{0xec, 0xc7, 0xda, 0xa2};
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // Premature end of data, invalid
    witness_commitment_section_325.push_back(0x01);
    witness_commitment_section_325.push_back(0x51);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // has data, valid
    witness_commitment_section_325.push_back(0x00);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(SignetTxs::Create(block, challenge));
    BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

    // Extraneous data, invalid
    witness_commitment_section_325.push_back(0x00);
    cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
    block.vtx.at(0) = MakeTransactionRef(cb);
    BOOST_CHECK(!SignetTxs::Create(block, challenge));
    BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));
}

//! Test retrieval of valid assumeutxo values.
BOOST_AUTO_TEST_CASE(test_assumeutxo)
{
    const auto params = CreateChainParams(*m_node.args, CBaseChainParams::REGTEST);

    // These heights don't have assumeutxo configurations associated, per the contents
    // of kernel/chainparams.cpp.
    std::vector<int> bad_heights{0, 100, 111, 115, 209, 211};

    for (auto empty : bad_heights) {
        const auto out = ExpectedAssumeutxo(empty, *params);
        BOOST_CHECK(!out);
    }

    const auto out110 = *ExpectedAssumeutxo(110, *params);
    BOOST_CHECK_EQUAL(out110.hash_serialized.ToString(), "dc81af66a58085fe977c6aab56b49630d87b84521fc5a8a5c53f2f4b23c8d6d5");
    BOOST_CHECK_EQUAL(out110.nChainTx, 110U);

    const auto out210 = *ExpectedAssumeutxo(200, *params);
    BOOST_CHECK_EQUAL(out210.hash_serialized.ToString(), "51c8d11d8b5c1de51543c579736e786aa2736206d1e11e627568029ce092cf62");
    BOOST_CHECK_EQUAL(out210.nChainTx, 200U);
}

BOOST_AUTO_TEST_SUITE_END()
