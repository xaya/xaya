// Copyright (c) 2018-2023 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <powdata.h>

#include <arith_uint256.h>
#include <consensus/params.h>
#include <logging.h>

#include <sstream>
#include <stdexcept>

int
powAlgoLog2Weight (const PowAlgo algo)
{
  switch (algo)
    {
    case PowAlgo::SHA256D:
      return 0;
    case PowAlgo::NEOSCRYPT:
      return 10;
    default:
      assert (false);
    }
}

uint256
powLimitForAlgo (const PowAlgo algo, const Consensus::Params& params)
{
  /* Special rule for regtest net:  Always just return the minimal powLimit
     from the chain params.  */
  if (params.fPowNoRetargeting)
    return params.powLimitNeoscrypt;

  arith_uint256 result = UintToArith256 (params.powLimitNeoscrypt);
  const int log2Diff = powAlgoLog2Weight (PowAlgo::NEOSCRYPT)
                        - powAlgoLog2Weight (algo);
  assert (log2Diff >= 0);
  result >>= log2Diff;

  return ArithToUint256 (result);
}

PowAlgo
PowAlgoFromString (const std::string& str)
{
  if (str == "sha256d")
    return PowAlgo::SHA256D;
  if (str == "neoscrypt")
    return PowAlgo::NEOSCRYPT;
  throw std::invalid_argument ("invalid PowAlgo: '" + str + "'");
}

std::string
PowAlgoToString (const PowAlgo algo)
{
  switch (algo)
    {
    case PowAlgo::SHA256D:
      return "sha256d";
    case PowAlgo::NEOSCRYPT:
      return "neoscrypt";
    default:
      {
        std::ostringstream msg;
        msg << "can't convert PowAlgo "
            << static_cast<int> (algo)
            << " to string";
        throw std::invalid_argument (msg.str ());
      }
    }
}

void
PowData::setCoreAlgo (const PowAlgo a)
{
  int newAlgo = static_cast<int> (a);
  newAlgo &= ~mmFlag;
  if (isMergeMined ())
    newAlgo |= mmFlag;
  algo = static_cast<PowAlgo> (newAlgo);
}

void
PowData::setFakeHeader (std::unique_ptr<CPureBlockHeader> hdr)
{
  /* Clear merge-mining flag (if it was set).  */
  algo = getCoreAlgo ();
  assert (algo == getCoreAlgo ());
  assert (!isMergeMined ());

  auxpow.reset ();
  fakeHeader.reset (hdr.release ());
}

CPureBlockHeader&
PowData::initFakeHeader (const CPureBlockHeader& block)
{
  std::unique_ptr<CPureBlockHeader> hdr(new CPureBlockHeader ());
  hdr->SetNull ();
  hdr->hashMerkleRoot = block.GetBaseHash ();
  setFakeHeader (std::move (hdr));
  return *fakeHeader;
}

void
PowData::setAuxpow (std::unique_ptr<CAuxPow> apow)
{
  /* Add merge-mining flag to algo.  */
  const int coreAlgo = static_cast<int> (getCoreAlgo ());
  algo = static_cast<PowAlgo> (coreAlgo | mmFlag);
  assert (static_cast<PowAlgo> (coreAlgo) == getCoreAlgo ());
  assert (isMergeMined ());

  fakeHeader.reset ();
  auxpow.reset (apow.release ());
}

CPureBlockHeader&
PowData::initAuxpow (const CPureBlockHeader& block)
{
  setAuxpow (CAuxPow::createAuxPow (block));
  assert (auxpow != nullptr);
  return auxpow->parentBlock;
}

bool
PowData::isValid (const uint256& hash, const Consensus::Params& params) const
{
  switch (getCoreAlgo ())
    {
    case PowAlgo::SHA256D:
      if (!isMergeMined ())
        return error ("%s: SHA256D must be merge-mined", __func__);
      break;
    case PowAlgo::NEOSCRYPT:
      if (isMergeMined ())
        return error ("%s: Neoscrypt cannot be merge-mined", __func__);
      break;
    default:
      return error ("%s: invalid mining algo used for PoW", __func__);
    }

  if (isMergeMined ())
    {
      if (auxpow == nullptr)
        return error ("%s: merge-mined PoW data has no auxpow", __func__);
      if (!checkProofOfWork (auxpow->parentBlock, params))
        return error ("%s: auxpow PoW is invalid", __func__);
      if (!auxpow->check (hash, params.nAuxpowChainId, params))
        return error ("%s: auxpow is invalid", __func__);
    }
  else
    {
      if (fakeHeader == nullptr)
        return error ("%s: stand-alone PoW data has no fake header", __func__);
      if (fakeHeader->hashMerkleRoot != hash)
        return error ("%s: fake header commits to wrong hash", __func__);
      if (!checkProofOfWork (*fakeHeader, params))
        return error ("%s: fake header PoW is invalid", __func__);
    }

  return true;
}

bool
PowData::checkProofOfWork (const CPureBlockHeader& hdr,
                           const Consensus::Params& params) const
{
  const PowAlgo algo = getCoreAlgo ();
  return checkProofOfWork (algo, hdr.GetPowHash (algo), getBits (), params);
}

bool
PowData::checkProofOfWork (const PowAlgo algo,
                           const uint256& hash, const unsigned nBits,
                           const Consensus::Params& params)
{
  bool fNegative;
  bool fOverflow;
  arith_uint256 bnTarget;

  bnTarget.SetCompact (nBits, &fNegative, &fOverflow);

  // Check range
  if (fNegative || bnTarget == 0 || fOverflow
          || bnTarget > UintToArith256 (powLimitForAlgo (algo, params)))
    return false;

  // Check proof of work matches claimed amount
  if (UintToArith256 (hash) > bnTarget)
    return false;

  return true;
}
