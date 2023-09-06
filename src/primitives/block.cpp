// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>

/* 2023-10-15 midnight UTC */
constexpr int64_t RNG_SEED_BLOCKHASH_FORK_TIME = 1697328000;

uint256
CBlockHeader::GetRngSeed () const
{
  /* Previously, we based random numbers on the PoW instead of the block hash.
     This makes it harder to brute-force desired outcomes for miners.
     However, the PoW is not part of network consensus, so this does not
     (necessarily) establish consensus over the random numbers.

     Hence, with a fork, we switch the RNG seed to the block hash (or rather,
     hash of the block hash, to ensure a uniform distribution in any case).

     It would be possible to use the PoW again, if it was made part of
     network consensus (for instance, the block hash could commit to the PoW,
     with the PoW itself referring to the block by a hash not including the
     PoW).  This likely requires a network hard fork, and thus it is not
     yet done.  */

  if (nTime >= RNG_SEED_BLOCKHASH_FORK_TIME)
    return Hash (GetHash ());

  uint256 powHash;
  if (pow.isMergeMined ())
    powHash = pow.getAuxpow ().getParentBlockHash ();
  else
    powHash = pow.getFakeHeader ().GetHash ();
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
