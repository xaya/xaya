// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include "checkpoints.h"

#include "uint256.h"
#include "test/test_bitcoin.h"
#include "chainparams.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(Checkpoints_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(sanity)
{
    const Checkpoints::CCheckpointData& checkpoints = Params(CBaseChainParams::MAIN).Checkpoints();
    const uint256 p2016 = uint256S("0x0000000000660bad0d9fbde55ba7ee14ddf766ed5f527e3fbca523ac11460b92");
    const uint256 p4032 = uint256S("0x0000000000493b5696ad482deb79da835fe2385304b841beef1938655ddbc411");
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 2016, p2016));
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 4032, p4032));

    
    // Wrong hashes at checkpoints should fail:
    BOOST_CHECK(!Checkpoints::CheckBlock(checkpoints, 2016, p4032));
    BOOST_CHECK(!Checkpoints::CheckBlock(checkpoints, 4032, p2016));

    // ... but any hash not at a checkpoint should succeed:
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 2016+1, p4032));
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 4032+1, p2016));

    // FIXME: reenable
    //BOOST_CHECK(Checkpoints::GetTotalBlocksEstimate() >= 134444);
}    

BOOST_AUTO_TEST_SUITE_END()
