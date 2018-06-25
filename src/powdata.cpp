// Copyright (c) 2018 The Xyon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <powdata.h>

#include <arith_uint256.h>
#include <util.h>

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

  fakeHeader.reset (hdr.release ());
}

CPureBlockHeader&
PowData::initFakeHeader (const CPureBlockHeader& block)
{
  std::unique_ptr<CPureBlockHeader> hdr(new CPureBlockHeader ());
  hdr->SetNull ();
  hdr->hashMerkleRoot = block.GetHash ();
  setFakeHeader (std::move (hdr));
  return *fakeHeader;
}

bool
PowData::isValid (const uint256& hash, const Consensus::Params& params) const
{
  if (isMergeMined ())
    return error ("%s: merge-mining is not supported", __func__);

  switch (getCoreAlgo ())
    {
    case PowAlgo::SHA256D:
    case PowAlgo::NEOSCRYPT:
      /* These are the valid algos.  */
      break;
    default:
      return error ("%s: invalid mining algo used for PoW", __func__);
    }

  if (fakeHeader == nullptr)
    return error ("%s: PoW data has no fake header", __func__);
  if (fakeHeader->hashMerkleRoot != hash)
    return error ("%s: fake header commits to wrong hash", __func__);
  if (!checkProofOfWork (*fakeHeader, params))
    return error ("%s: fake header PoW is invalid", __func__);

  return true;
}

bool
PowData::checkProofOfWork (const CPureBlockHeader& hdr,
                           const Consensus::Params& params) const
{
  const PowAlgo coreAlgo = getCoreAlgo ();

  /* The code below is CheckProofOfWork from upstream's pow.cpp.  It has been
     moved here so that powdata.cpp does not depend on pow.cpp, which is
     in the "server" library.  */

  bool fNegative, fOverflow;
  arith_uint256 bnTarget;
  bnTarget.SetCompact (getBits (), &fNegative, &fOverflow);

  // Check range
  if (fNegative || bnTarget == 0 || fOverflow
        || bnTarget > UintToArith256 (powLimitForAlgo (coreAlgo, params)))
    return false;

  // Check proof of work matches claimed amount
  if (UintToArith256 (hdr.GetPowHash (coreAlgo)) > bnTarget)
    return false;

  return true;
}
