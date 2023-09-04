// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_PUREHEADER_H
#define BITCOIN_PRIMITIVES_PUREHEADER_H

#include <serialize.h>
#include <uint256.h>
#include <util/time.h>

#include <vector>

enum class PowAlgo : uint8_t;

/**
 * A block header without auxpow information.  This "intermediate step"
 * in constructing the full header is useful, because it breaks the cyclic
 * dependency between auxpow (referencing a parent block header) and
 * the block header (referencing an auxpow).  The parent block header
 * does not have auxpow itself, so it is a pure header.
 */
class CPureBlockHeader
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CPureBlockHeader()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CPureBlockHeader, obj) { READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce); }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nTime == 0);
    }

    /**
     * Returns the base hash, which is the "ordinary" hash of the pure header
     * (and thus does not include the CBlockHeader specific PowData).
     *
     * The base hash is used to construct the PoW, i.e. the PoW commits to
     * the base hash to ensure the basic data of the Xaya block is verified
     * by it.
     */
    uint256 GetBaseHash() const;

    uint256 GetPowHash(PowAlgo algo) const;

    NodeSeconds Time() const
    {
        return NodeSeconds{std::chrono::seconds{nTime}};
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};

/**
 * Swaps the endian-ness of each 4-byte word in the given vector of bytes.
 * This is used for getwork and also for our neoscrypt PoW hash.
 */
void SwapGetWorkEndianness (std::vector<unsigned char>& data);

#endif // BITCOIN_PRIMITIVES_PUREHEADER_H
