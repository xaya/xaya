// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include "chainparamsbase.h"
#include "checkpoints.h"
#include "primitives/block.h"
#include "protocol.h"
#include "arith_uint256.h"

#include <map>
#include <vector>
#include <utility>

typedef unsigned char MessageStartChars[MESSAGE_START_SIZE];

struct CDNSSeedData {
    std::string name, host;
    CDNSSeedData(const std::string &strName, const std::string &strHost) : name(strName), host(strHost) {}
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        //EXT_PUBLIC_KEY,
        //EXT_SECRET_KEY,

        MAX_BASE58_TYPES
    };

    enum BugType {
        /* Tx is valid and all nameops should be performed.  */
        BUG_FULLY_APPLY,
        /* Don't apply the name operations but put the names into the UTXO
           set.  This is done for libcoin's "d/bitcoin" stealing.  It is
           then used as input into the "d/wav" stealing, thus needs to be in
           the UTXO set.  We don't want the name to show up in the name
           database, though.  */
        BUG_IN_UTXO,
        /* Don't apply the name operations and don't put the names into the
           UTXO set.  They are immediately unspendable.  This is used for the
           "d/wav" stealing output (which is not used later on) and also
           for the NAME_FIRSTUPDATE's that are in non-Namecoin tx.  */
        BUG_FULLY_IGNORE,
    };

    const uint256& HashGenesisBlock() const { return hashGenesisBlock; }
    const MessageStartChars& MessageStart() const { return pchMessageStart; }
    const std::vector<unsigned char>& AlertKey() const { return vAlertPubKey; }
    int GetDefaultPort() const { return nDefaultPort; }
    const arith_uint256& ProofOfWorkLimit() const { return bnProofOfWorkLimit; }
    int SubsidyHalvingInterval() const { return nSubsidyHalvingInterval; }
    /** Used to check majorities for block version upgrade */
    int EnforceBlockUpgradeMajority() const { return nEnforceBlockUpgradeMajority; }
    int RejectBlockOutdatedMajority() const { return nRejectBlockOutdatedMajority; }
    int ToCheckBlockUpgradeMajority() const { return nToCheckBlockUpgradeMajority; }

    /** Used if GenerateBitcoins is called with a negative number of threads */
    int DefaultMinerThreads() const { return nMinerThreads; }
    const CBlock& GenesisBlock() const { return genesis; }
    bool RequireRPCPassword() const { return fRequireRPCPassword; }
    /** Make miner wait to have peers to avoid wasting work */
    bool MiningRequiresPeers() const { return fMiningRequiresPeers; }
    /** Default value for -checkmempool argument */
    bool DefaultCheckMemPool() const { return fDefaultCheckMemPool; }
    /** Default value for -checknamedb argument */
    virtual int DefaultCheckNameDB() const = 0;
    /** Allow mining of a min-difficulty block */
    bool AllowMinDifficultyBlocks(const CBlockHeader& block) const;
    /** Skip proof-of-work check: allow mining of any difficulty block */
    bool SkipProofOfWorkCheck() const { return fSkipProofOfWorkCheck; }
    /** Make standard checks */
    bool RequireStandard() const { return fRequireStandard; }
    int64_t TargetTimespan() const { return nTargetTimespan; }
    int64_t TargetSpacing() const { return nTargetSpacing; }
    int64_t Interval() const { return nTargetTimespan / nTargetSpacing; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }
    /** In the future use NetworkIDString() for RPC fields */
    bool TestnetToBeDeprecatedFieldRPC() const { return fTestnetToBeDeprecatedFieldRPC; }
    /** Return the BIP70 network string (main, test or regtest) */
    std::string NetworkIDString() const { return strNetworkID; }
    const std::vector<CDNSSeedData>& DNSSeeds() const { return vSeeds; }
    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const std::vector<CAddress>& FixedSeeds() const { return vFixedSeeds; }
    virtual const Checkpoints::CCheckpointData& Checkpoints() const = 0;

    /* Return the auxpow chain ID.  */
    inline int32_t AuxpowChainId () const { return 0x0001; }

    /* Return start height of auxpow and the retarget interval change.  */
    virtual int AuxpowStartHeight() const = 0;

    /* Return whether or not to enforce strict chain ID checks.  */
    virtual bool StrictChainId() const = 0;

    /* Return whether to allow blocks with a "legacy" version.  */
    virtual bool AllowLegacyBlocks(unsigned nHeight) const = 0;

    /* Return the expiration depth for names at the given height.  */
    virtual unsigned NameExpirationDepth(unsigned nHeight) const = 0;

    /* Return whether to allow lenient NAME_NEW version check.  */
    virtual bool LenientVersionCheck(unsigned nHeight) const = 0;

    /* Check whether the given tx is a "historic relic" for which to
       skip the validity check.  Return also the "type" of the bug,
       which determines further actions.  */
    bool IsHistoricBug(const uint256& txid, unsigned nHeight, BugType& type) const;

protected:
    CChainParams() {}

    uint256 hashGenesisBlock;
    MessageStartChars pchMessageStart;
    //! Raw pub key bytes for the broadcast alert signing key.
    std::vector<unsigned char> vAlertPubKey;
    int nDefaultPort;
    arith_uint256 bnProofOfWorkLimit;
    int nSubsidyHalvingInterval;
    int nEnforceBlockUpgradeMajority;
    int nRejectBlockOutdatedMajority;
    int nToCheckBlockUpgradeMajority;
    int64_t nTargetTimespan;
    int64_t nTargetSpacing;
    int nMinerThreads;
    std::vector<CDNSSeedData> vSeeds;
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    std::string strNetworkID;
    CBlock genesis;
    std::vector<CAddress> vFixedSeeds;
    bool fRequireRPCPassword;
    bool fMiningRequiresPeers;
    bool fDefaultCheckMemPool;
    bool fAllowMinDifficultyBlocks;
    unsigned nMinDifficultySince;
    bool fRequireStandard;
    bool fMineBlocksOnDemand;
    bool fSkipProofOfWorkCheck;
    bool fTestnetToBeDeprecatedFieldRPC;

    /* Map (block height, txid) pairs for buggy transactions onto their
       bug type value.  */
    std::map<std::pair<unsigned, uint256>, BugType> mapHistoricBugs;

    /* Utility routine to insert into historic bug map.  */
    inline void addBug(unsigned nHeight, const char* txid, BugType type)
    {
        std::pair<unsigned, uint256> key(nHeight, uint256(txid));
        mapHistoricBugs.insert(std::make_pair(key, type));
    }
};

/** 
 * Modifiable parameters interface is used by test cases to adapt the parameters in order
 * to test specific features more easily. Test cases should always restore the previous
 * values after finalization.
 */

class CModifiableParams {
public:
    //! Published setters to allow changing values in unit test cases
    virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval) =0;
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority)=0;
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority)=0;
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority)=0;
    virtual void setDefaultCheckMemPool(bool aDefaultCheckMemPool)=0;
    virtual void setAllowMinDifficultyBlocks(bool aAllowMinDifficultyBlocks)=0;
    virtual void setSkipProofOfWorkCheck(bool aSkipProofOfWorkCheck)=0;
    virtual void setProofOfWorkLimit(const arith_uint256& limit)=0;
};


/**
 * Return the currently selected parameters. This won't change after app startup
 * outside of the unit tests.
 */
const CChainParams &Params();

/** Return parameters for the given network. */
CChainParams &Params(CBaseChainParams::Network network);

/** Get modifiable network parameters (UNITTEST only) */
CModifiableParams *ModifiableParams();

/** Sets the params returned by Params() to those for the given network. */
void SelectParams(CBaseChainParams::Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

#endif // BITCOIN_CHAINPARAMS_H
