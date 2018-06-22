// Copyright (c) 2018 The Xyon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/params.h>
#include <pow.h>
#include <powdata.h>
#include <primitives/block.h>
#include <primitives/pureheader.h>
#include <streams.h>
#include <utilstrencodings.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>

/* No space between BOOST_FIXTURE_TEST_SUITE and '(', so that extraction of
   the test-suite name works with grep as done in the Makefile.  */
BOOST_FIXTURE_TEST_SUITE(powdata_tests, TestingSetup)

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE (powalgo_to_string)
{
  BOOST_CHECK_EQUAL (PowAlgoToString (PowAlgo::SHA256D), "sha256d");
  BOOST_CHECK_EQUAL (PowAlgoToString (PowAlgo::NEOSCRYPT), "neoscrypt");
  BOOST_CHECK_THROW (PowAlgoToString (PowAlgo::INVALID), std::invalid_argument);
  BOOST_CHECK_THROW (PowAlgoToString (PowAlgo::FLAG_MERGE_MINED),
                     std::invalid_argument);
}

BOOST_AUTO_TEST_CASE (powalgo_from_string)
{
  BOOST_CHECK (PowAlgoFromString ("sha256d") == PowAlgo::SHA256D);
  BOOST_CHECK (PowAlgoFromString ("neoscrypt") == PowAlgo::NEOSCRYPT);
  BOOST_CHECK_THROW (PowAlgoFromString (""), std::invalid_argument);
  BOOST_CHECK_THROW (PowAlgoFromString ("foo"), std::invalid_argument);
}

/* ************************************************************************** */

namespace
{

PowData
CheckPowRoundtrip (const std::string& hex)
{
  CDataStream stream(ParseHex (hex), SER_NETWORK, PROTOCOL_VERSION);
  PowData powData;
  stream >> powData;

  std::vector<unsigned char> serialised;
  CVectorWriter writer(SER_NETWORK, PROTOCOL_VERSION, serialised, 0);
  writer << powData;

  BOOST_CHECK_EQUAL (HexStr (serialised.begin (), serialised.end ()), hex);

  return powData;
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE (serialisation_standalone)
{
  const PowData powData = CheckPowRoundtrip (
      "02"
      "12345678"
      "00000000"
      "0000000000000000000000000000000000000000000000000000000000000000"
      "1234000000000000000000000000000000000000000000000000000000005678"
      "00000000"
      "00000000"
      "123abcde");

  BOOST_CHECK (!powData.isMergeMined ());
  BOOST_CHECK (powData.getCoreAlgo () == PowAlgo::NEOSCRYPT);
  BOOST_CHECK_EQUAL (powData.getBits (), 0x78563412);

  const auto& fakeHeader = powData.getFakeHeader ();
  BOOST_CHECK_EQUAL (fakeHeader.nNonce, 0xdebc3a12);
  BOOST_CHECK_EQUAL (
      fakeHeader.hashMerkleRoot.GetHex (),
      "7856000000000000000000000000000000000000000000000000000000003412");
}

/* ************************************************************************** */

namespace
{

void
MineFakeHeader (CPureBlockHeader& hdr, const PowData& data,
                const Consensus::Params& params, const bool ok)
{
  while (CheckProofOfWork (hdr.GetPowHash (data.getCoreAlgo ()),
                           data.getBits (), params)
            != ok)
    ++hdr.nNonce;
}

constexpr uint32_t bitsRegtest = 0x207fffff;
constexpr uint32_t bitsMainnet = 0x1e0ffff0;

}

BOOST_AUTO_TEST_CASE (validation_fakeHeader)
{
  /* Use regtest parameters to allow mining with easy difficulty.  */
  SelectParams (CBaseChainParams::REGTEST);
  const auto& params = Params ().GetConsensus ();

  CBlockHeader block;
  block.nTime = 1234;
  const uint256 hash = block.GetHash ();

  PowData powTmpl;
  powTmpl.setCoreAlgo (PowAlgo::NEOSCRYPT);
  powTmpl.setBits (bitsRegtest);

  /* No fake header set, should be invalid.  */
  BOOST_CHECK (!powTmpl.isValid (hash, params));

  /* Valid PoW but not committing to the block hash.  */
  {
    PowData pow(powTmpl);
    std::unique_ptr<CPureBlockHeader> fakeHeader(new CPureBlockHeader ());
    MineFakeHeader (*fakeHeader, pow, params, true);
    pow.setFakeHeader (std::move (fakeHeader));
    BOOST_CHECK (pow.isValid (uint256 (), params));
    BOOST_CHECK (!pow.isValid (hash, params));
  }

  /* Correct PoW commitment.  */
  {
    PowData pow(powTmpl);
    auto& fakeHeader = pow.initFakeHeader (block);
    MineFakeHeader (fakeHeader, pow, params, false);
    BOOST_CHECK (!pow.isValid (hash, params));
    MineFakeHeader (fakeHeader, pow, params, true);
    BOOST_CHECK (pow.isValid (hash, params));
  }

  /* The PoW is (very likely) still invalid for higher difficulty.  */
  {
    PowData pow(powTmpl);
    auto& fakeHeader = pow.initFakeHeader (block);
    MineFakeHeader (fakeHeader, pow, params, true);
    pow.setBits (bitsMainnet);
    BOOST_CHECK (!pow.isValid (hash, params));
  }
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
