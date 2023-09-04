// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>

uint256
CBlockHeader::GetHash () const
{
  /* FIXME: Actually set fork time to not just the genesis block.  */
  if (nTime > 1601286749)
    return SerializeHash(*this);

  return GetBaseHash();
}

uint256
CBlockHeader::GetRngSeed () const
{
  /* Random numbers in games must not be based off the block hash itself, as
     this can be trivially brute-forced by miners *before* they even attempt
     the PoW attached to it.  Because of that, the seed for random numbers
     should be derived from the PoW itself, i.e. the "fake header" or the
     auxpow parent block.  Furthermore, to avoid the bias towards smaller
     numbers that exists with SHA256D-mined auxpow headers, we hash the
     PoW hash again to get a uniform distribution.  */

  uint256 powHash;
  if (pow.isMergeMined ())
    powHash = pow.getAuxpow ().getParentBlockHash ();
  else
    powHash = pow.getFakeHeader ().GetBaseHash ();
  assert (!powHash.IsNull ());

  return Hash (powHash);
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
