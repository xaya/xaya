// Copyright (c) 2018 The Xyon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <powdata.h>

#include <pow.h>
#include <util.h>

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
  if (algo != PowAlgo::NEOSCRYPT)
    return error ("%s: only neoscrypt is supported for now", __func__);
  if (fakeHeader == nullptr)
    return error ("%s: PoW data has no fake header", __func__);

  if (fakeHeader->hashMerkleRoot != hash)
    return error ("%s: fake header commits to wrong hash", __func__);
  if (!CheckProofOfWork (fakeHeader->GetPowHash (), nBits, params))
    return error ("%s: fake header PoW is invalid", __func__);

  return true;
}
