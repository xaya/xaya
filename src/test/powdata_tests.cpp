// Copyright (c) 2018-2023 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <pow.h>
#include <powdata.h>
#include <primitives/block.h>
#include <primitives/pureheader.h>
#include <streams.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <test/util/setup_common.h>

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

BOOST_AUTO_TEST_CASE (powlimit_for_algo_mainnet)
{
  const auto& params = Params ().GetConsensus ();
  const arith_uint256 limitSha256
      = UintToArith256 (powLimitForAlgo (PowAlgo::SHA256D, params));
  const arith_uint256 limitNeoscrypt
      = UintToArith256 (powLimitForAlgo (PowAlgo::NEOSCRYPT, params));
  BOOST_CHECK (ArithToUint256 (limitNeoscrypt) == params.powLimitNeoscrypt);
  BOOST_CHECK (limitNeoscrypt > limitSha256);
  BOOST_CHECK (limitSha256 == limitNeoscrypt / 1024);
}

BOOST_AUTO_TEST_CASE (powlimit_for_algo_regtest)
{
  SelectParams (ChainType::REGTEST);
  const auto& params = Params ().GetConsensus ();
  BOOST_CHECK (powLimitForAlgo (PowAlgo::SHA256D, params)
                == params.powLimitNeoscrypt);
  BOOST_CHECK (powLimitForAlgo (PowAlgo::SHA256D, params)
                == powLimitForAlgo (PowAlgo::NEOSCRYPT, params));
}

/* ************************************************************************** */

namespace
{

PowData
CheckPowRoundtrip (const std::string& hex)
{
  DataStream stream(ParseHex (hex));
  PowData powData;
  stream >> powData;

  std::vector<unsigned char> serialised;
  VectorWriter writer(serialised, 0);
  writer << powData;

  BOOST_CHECK_EQUAL (HexStr (serialised), hex);

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

BOOST_AUTO_TEST_CASE (serialisation_auxpow)
{
  const PowData powData = CheckPowRoundtrip (
      "81"
      "12345678"

      /* Data constructed using auxpow.py's computeAuxpow.  */
      "010000000100000000000000000000000000000000000000000000000000000000000000"
      "00ffffffff2cfabe6d6d0ee43f37fdbe12d806ed691820564f40e054172166f67d5ac12f"
      "9230beba6d870100000000000000ffffffff000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000001000000000000"
      "0000000000000000000000000000000000000000000000000000000000af4496476c8483"
      "b9a329da9299b87ee2885e5f9c51c0107f103f7d7af013afbe0000000000000000000000"
      "01"
  );

  BOOST_CHECK (powData.isMergeMined ());
  BOOST_CHECK (powData.getCoreAlgo () == PowAlgo::SHA256D);
  BOOST_CHECK_EQUAL (powData.getBits (), 0x78563412);

  const auto& auxpow = powData.getAuxpow ();
  BOOST_CHECK_EQUAL (
      auxpow.getParentBlockHash ().GetHex (),
      "15138e015573ed13094bb1c0b23e72f05f803f0089dfb4a44a31e84d27df2e48");
}

/* ************************************************************************** */

/**
 * Friend class of CAuxPow that is used in the tests below to manipulate the
 * auxpow to make it invalid.
 */
class CAuxPowForTest
{
public:

  static void
  invalidateAuxpow (CAuxPow& auxpow)
  {
    ++auxpow.nChainIndex;
  }

};

/**
 * Friend class of PowData to give access to internals.
 */
class PowDataForTest : public PowData
{
public:

  using PowData::auxpow;

};

namespace
{

constexpr uint32_t bitsRegtest = 0x207fffff;
constexpr uint32_t bitsMainnet = 0x1e0ffff0;

void
MineHeader (CPureBlockHeader& hdr, const PowData& data,
            const Consensus::Params& params, const bool ok)
{
  while (data.checkProofOfWork (hdr, params) != ok)
    ++hdr.nNonce;
}

class ValidationSetup : public TestingSetup
{
public:

  const Consensus::Params& params;

  CBlockHeader block;
  uint256 hash;

  PowDataForTest pow;

  ValidationSetup ()
    : TestingSetup(ChainType::REGTEST),
      params(Params ().GetConsensus ())
  {
    block.nTime = 1234;
    hash = block.GetHash ();

    pow.setCoreAlgo (PowAlgo::NEOSCRYPT);
    pow.setBits (bitsRegtest);
  }

};

class ValidationSetupSha : public ValidationSetup
{
public:

  ValidationSetupSha ()
    : ValidationSetup ()
  {
    pow.setCoreAlgo (PowAlgo::SHA256D);
  }

};

} // anonymous namespace

/* Tests for the rules about which algos must be merge vs stand-alone mined.  */

BOOST_FIXTURE_TEST_CASE (mmAlgoCheck_neoscrypt, ValidationSetup)
{
  pow.setCoreAlgo (PowAlgo::NEOSCRYPT);

  auto& fakeHeader = pow.initFakeHeader (block);
  MineHeader (fakeHeader, pow, params, true);
  BOOST_CHECK (!pow.isMergeMined ());
  BOOST_CHECK (pow.isValid (hash, params));

  auto& hdr = pow.initAuxpow (block);
  MineHeader (hdr, pow, params, true);
  BOOST_CHECK (pow.isMergeMined ());
  BOOST_CHECK (!pow.isValid (hash, params));
}

BOOST_FIXTURE_TEST_CASE (mmAlgoCheck_sha256d, ValidationSetup)
{
  pow.setCoreAlgo (PowAlgo::SHA256D);

  auto& fakeHeader = pow.initFakeHeader (block);
  MineHeader (fakeHeader, pow, params, true);
  BOOST_CHECK (!pow.isMergeMined ());
  BOOST_CHECK (!pow.isValid (hash, params));

  auto& hdr = pow.initAuxpow (block);
  MineHeader (hdr, pow, params, true);
  BOOST_CHECK (pow.isMergeMined ());
  BOOST_CHECK (pow.isValid (hash, params));
}

/* Tests for validation of a fake-header PoW.  */

BOOST_FIXTURE_TEST_CASE (fakeHeader_unset, ValidationSetup)
{
  pow.setFakeHeader (nullptr);
  BOOST_CHECK (!pow.isValid (hash, params));
}

BOOST_FIXTURE_TEST_CASE (fakeHeader_wrongHash, ValidationSetup)
{
  std::unique_ptr<CPureBlockHeader> fakeHeader(new CPureBlockHeader ());
  MineHeader (*fakeHeader, pow, params, true);
  pow.setFakeHeader (std::move (fakeHeader));
  BOOST_CHECK (pow.isValid (uint256 (), params));
  BOOST_CHECK (!pow.isValid (hash, params));
}

BOOST_FIXTURE_TEST_CASE (fakeHeader_pow, ValidationSetup)
{
  auto& fakeHeader = pow.initFakeHeader (block);
  MineHeader (fakeHeader, pow, params, false);
  BOOST_CHECK (!pow.isValid (hash, params));
  MineHeader (fakeHeader, pow, params, true);
  BOOST_CHECK (pow.isValid (hash, params));
}

/* Tests for validation of a merge-mined PoW.  */

BOOST_FIXTURE_TEST_CASE (auxpow_unset, ValidationSetupSha)
{
  pow.setAuxpow (nullptr);
  BOOST_CHECK (!pow.isValid (hash, params));
}

BOOST_FIXTURE_TEST_CASE (auxpow_invalid, ValidationSetupSha)
{
  auto& hdr = pow.initAuxpow (block);
  MineHeader (hdr, pow, params, true);
  BOOST_CHECK (!pow.isValid (uint256 (), params));
  BOOST_CHECK (pow.isValid (hash, params));
  CAuxPowForTest::invalidateAuxpow (*pow.auxpow);
  BOOST_CHECK (!pow.isValid (hash, params));
}

BOOST_FIXTURE_TEST_CASE (auxpow_pow, ValidationSetupSha)
{
  auto& hdr = pow.initAuxpow (block);
  MineHeader (hdr, pow, params, false);
  BOOST_CHECK (!pow.isValid (hash, params));
  MineHeader (hdr, pow, params, true);
  BOOST_CHECK (pow.isValid (hash, params));
}

/* Tests for the checkProofOfWork method itself.  */

namespace
{

class CheckPowSetup : public ValidationSetup
{
public:

  CPureBlockHeader hdr;

};

/* Since the regtest difficulty is so low that we may (50% of the time) satisfy
   (or not) a PoW accidentally, some tests repeated to make sure the underlying
   behaviour is really correct.  */
constexpr unsigned repeats = 10;

} // anonymous namespace

BOOST_FIXTURE_TEST_CASE (checkPow_neoscrypt, CheckPowSetup)
{
  MineHeader (hdr, pow, params, false);
  BOOST_CHECK (!pow.checkProofOfWork (hdr, params));
  MineHeader (hdr, pow, params, true);
  BOOST_CHECK (pow.checkProofOfWork (hdr, params));
}

BOOST_FIXTURE_TEST_CASE (checkPow_highDifficulty, CheckPowSetup)
{
  MineHeader (hdr, pow, params, true);
  pow.setBits (bitsMainnet);
  BOOST_CHECK (!pow.checkProofOfWork (hdr, params));
}

BOOST_FIXTURE_TEST_CASE (checkPow_sha256d, CheckPowSetup)
{
  for (unsigned i = 0; i < repeats; ++i)
    {
      pow.setCoreAlgo (PowAlgo::SHA256D);
      hdr.nTime = i;
      MineHeader (hdr, pow, params, true);
      BOOST_CHECK (pow.checkProofOfWork (hdr, params));
    }
}

BOOST_FIXTURE_TEST_CASE (checkPow_mismatchingAlgo, CheckPowSetup)
{
  bool foundMismatch = false;
  for (unsigned i = 0; i < repeats; ++i)
    {
      hdr.nTime = i;

      pow.setCoreAlgo (PowAlgo::NEOSCRYPT);
      MineHeader (hdr, pow, params, true);
      pow.setCoreAlgo (PowAlgo::SHA256D);

      if (!pow.checkProofOfWork (hdr, params))
        foundMismatch = true;
    }
  BOOST_CHECK (foundMismatch);
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
