// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

/** The maximum allowed size for a serialized block, in bytes (network rule) */
static const unsigned int MAX_BLOCK_SIZE = 1000000;

/**
 * Encapsulate a block version.  This takes care of building it up
 * from a base version, the modifier flags (like auxpow) and
 * also the auxpow chain ID.
 */
class CBlockVersion
{

private:

    /* Modifiers to the version.  */
    static const int32_t VERSION_AUXPOW = (1 << 8);

    /** Bits above are reserved for the auxpow chain ID.  */
    static const int32_t VERSION_CHAIN_START = (1 << 16);

    /** The version as integer.  Should not be accessed directly.  */
    int32_t nVersion;

public:

    inline CBlockVersion()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
    }

    inline void SetNull()
    {
        nVersion = 0;
    }

    /**
     * Set the base version (apart from chain ID and auxpow flag) to
     * the one given.  This should only be called when auxpow is not yet
     * set, to initialise a block!
     * @param nBaseVersion The base version.
     */
    void SetBaseVersion(int32_t nBaseVersion);

    /**
     * Extract the base version (without modifiers and chain ID).
     * @return The base version./
     */
    inline int32_t GetBaseVersion() const
    {
        return nVersion % VERSION_AUXPOW;
    }

    /**
     * Set the genesis block version.  This must be a literal write
     * through, to get the correct historic version.
     * @param nGenesisVersion The version to set.
     */
    inline void SetGenesisVersion(int32_t nGenesisVersion)
    {
        nVersion = nGenesisVersion;
    }

    /**
     * Extract the full version.  Used for RPC results and debug prints.
     * @return The full version.
     */
    inline int32_t GetFullVersion() const
    {
        return nVersion;
    }

    /**
     * Check if the auxpow flag is set in the version.
     * @return True iff this block version is marked as auxpow.
     */
    inline bool IsAuxpow() const
    {
        return nVersion & VERSION_AUXPOW;
    }

};

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    static const int32_t CURRENT_VERSION=3;
    CBlockVersion nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion.GetBaseVersion();
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    void SetNull()
    {
        nVersion.SetBaseVersion(CBlockHeader::CURRENT_VERSION);
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};


class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransaction> vtx;

    // memory only
    mutable std::vector<uint256> vMerkleTree;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CBlockHeader*)this);
        READWRITE(vtx);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        vMerkleTree.clear();
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        return block;
    }

    // Build the in-memory merkle tree for this block and return the merkle root.
    // If non-NULL, *mutated is set to whether mutation was detected in the merkle
    // tree (a duplication of transactions in the block leading to an identical
    // merkle root).
    uint256 BuildMerkleTree(bool* mutated = NULL) const;

    std::vector<uint256> GetMerkleBranch(int nIndex) const;
    static uint256 CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex);
    std::string ToString() const;
};


/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    CBlockLocator(const std::vector<uint256>& vHaveIn)
    {
        vHave = vHaveIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
