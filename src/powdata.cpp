// Copyright (c) 2018 The Xyon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <powdata.h>

#include <pow.h>
#include <util.h>

#include <sstream>
#include <stdexcept>

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
  if (!CheckProofOfWork (fakeHeader->GetPowHash (getCoreAlgo ()),
                         getBits (), params))
    return error ("%s: fake header PoW is invalid", __func__);

  return true;
}
