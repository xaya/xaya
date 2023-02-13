// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <consensus/amount.h>
#include <powdata.h>
#include <uint256.h>

#include <chrono>
#include <limits>
#include <map>
#include <vector>

#include <memory>

namespace Consensus {

/**
 * Identifiers for forks done on the network, so that validation code can
 * easily just query whether or not a particular fork should be active and
 * does not have to bother with the particular heights or other aspects.
 */
enum class Fork
{

  /**
   * Fork done after the token sale.  This removed the requirement that the
   * main (non-fakeheader) nonce must be zero in order to resolve
   * https://github.com/xaya/xaya/issues/50.
   *
   * It also increases the block reward from 1 CHI to a value calculated to
   * yield the correct total PoW supply.
   */
  POST_ICO,

};

/**
 * Interface for classes that define consensus behaviour in more
 * complex ways than just by a set of constants.
 */
class ConsensusRules
{
public:

    /* Provide a virtual destructor since we have virtual methods.  */
    virtual ~ConsensusRules() = default;

    /* Return minimum locked amount in a name.  */
    virtual CAmount MinNameCoinAmount(unsigned nHeight) const = 0;

    /**
     * Returns the target spacing (time in seconds between blocks) for blocks
     * of the given algorithm at the given height.
     */
    virtual std::chrono::seconds GetTargetSpacing(PowAlgo algo, unsigned height) const = 0;

    /**
     * Checks whether a given fork is in effect at the given block height.
     */
    virtual bool ForkInEffect(Fork type, unsigned height) const = 0;

};

class MainNetConsensus : public ConsensusRules
{
public:

    CAmount MinNameCoinAmount(unsigned nHeight) const override
    {
        return COIN / 100;
    }

    std::chrono::seconds GetTargetSpacing(const PowAlgo algo,
                                          const unsigned height) const override
    {
        if (!ForkInEffect (Fork::POST_ICO, height))
        {
            /* The target spacing is independent for each mining algorithm,
               so that the effective block frequency is half the value (with
               two algos).  */
            return std::chrono::seconds{2 * 30};
        }

        /* After the POST_ICO fork, the target spacing is changed to have
           still four blocks every two minutes (for an average of 30 seconds
           per block), but three of them standalone and only one merge-mined.
           This yields the desired 75%/25% split of block rewards.  */
        switch (algo)
        {
            case PowAlgo::SHA256D:
                return std::chrono::seconds{120};
            case PowAlgo::NEOSCRYPT:
                return std::chrono::seconds{40};
            default:
                assert(false);
        }
    }

    bool ForkInEffect(const Fork type, const unsigned height) const override
    {
        switch (type)
        {
            case Fork::POST_ICO:
                return height >= 440000;
            default:
                assert (false);
        }
    }

};

class TestNetConsensus : public MainNetConsensus
{
public:

    bool ForkInEffect(const Fork type, const unsigned height) const override
    {
        switch (type)
        {
            case Fork::POST_ICO:
                return height >= 11000;
            default:
                assert (false);
        }
    }

};

class RegTestConsensus : public TestNetConsensus
{
public:

    bool ForkInEffect(const Fork type, const unsigned height) const override
    {
        switch (type)
        {
            case Fork::POST_ICO:
                return height >= 500;
            default:
                assert (false);
        }
    }
};

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_P2SH,
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_TAPROOT, // Deployment of Schnorr/Taproot (BIPs 340-342)
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Initial block reward.  */
    CAmount initialSubsidy;
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, uint32_t> script_flag_exceptions;
    /** Block height at with BIP16 becomes active */
    int BIP16Height;
    /** Block height at which BIP34 becomes active */
    int BIP34Height;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimitNeoscrypt;
    bool fPowNoRetargeting;
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_P2SH:
            return BIP16Height;
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }

    /** Auxpow parameters */
    int32_t nAuxpowChainId;

    /** Consensus rule interface.  */
    std::unique_ptr<ConsensusRules> rules;
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
