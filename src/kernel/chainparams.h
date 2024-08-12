// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_CHAINPARAMS_H
#define BITCOIN_KERNEL_CHAINPARAMS_H

#include <consensus/params.h>
#include <kernel/messagestartchars.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/hash_type.h>
#include <util/vector.h>

#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

typedef std::map<int, uint256> MapCheckpoints;

struct CCheckpointData {
    MapCheckpoints mapCheckpoints;

    int GetHeight() const {
        const auto& final_checkpoint = mapCheckpoints.rbegin();
        return final_checkpoint->first /* height */;
    }
};

struct AssumeutxoHash : public BaseHash<uint256> {
    explicit AssumeutxoHash(const uint256& hash) : BaseHash(hash) {}
};

/**
 * Holds configuration for use during UTXO snapshot load and validation. The contents
 * here are security critical, since they dictate which UTXO snapshots are recognized
 * as valid.
 */
struct AssumeutxoData {
    int height;

    //! The expected hash of the deserialized UTXO set.
    AssumeutxoHash hash_serialized;

    //! Used to populate the m_chain_tx_count value, which is used during BlockManager::LoadBlockIndex().
    //!
    //! We need to hardcode the value here because this is computed cumulatively using block data,
    //! which we do not necessarily have at the time of snapshot load.
    uint64_t m_chain_tx_count;

    //! The hash of the base block for this snapshot. Used to refer to assumeutxo data
    //! prior to having a loaded blockindex.
    uint256 blockhash;
};

/**
 * Holds various statistics on transactions within a chain. Used to estimate
 * verification progress during chain sync.
 *
 * See also: CChainParams::TxData, GuessVerificationProgress.
 */
struct ChainTxData {
    int64_t nTime;    //!< UNIX timestamp of last known number of transactions
    uint64_t tx_count; //!< total number of transactions between genesis and that timestamp
    double dTxRate;   //!< estimated number of transactions per second after that timestamp
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system.
 */
class CChainParams
{
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

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

    const Consensus::Params& GetConsensus() const { return consensus; }
    const MessageStartChars& MessageStart() const { return pchMessageStart; }
    uint16_t GetDefaultPort() const { return nDefaultPort; }
    std::vector<int> GetAvailableSnapshotHeights() const;

    const CBlock& GenesisBlock() const { return genesis; }
    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }
    /** Default value for -checknamedb argument */
    virtual int DefaultCheckNameDB() const = 0;
    /** If this chain is exclusively used for testing */
    bool IsTestChain() const { return m_chain_type != ChainType::MAIN; }
    /** If this chain allows time to be mocked */
    bool IsMockableChain() const { return m_is_mockable_chain; }
    uint64_t PruneAfterHeight() const { return nPruneAfterHeight; }
    /** Minimum free space (in GB) needed for data directory */
    uint64_t AssumedBlockchainSize() const { return m_assumed_blockchain_size; }
    /** Minimum free space (in GB) needed for data directory when pruned; Does not include prune target*/
    uint64_t AssumedChainStateSize() const { return m_assumed_chain_state_size; }
    /** Whether it is possible to mine blocks on demand (no retargeting) */
    bool MineBlocksOnDemand() const { return consensus.fPowNoRetargeting; }
    /** Return the chain type string */
    std::string GetChainTypeString() const { return ChainTypeToString(m_chain_type); }
    /** Return the chain type */
    ChainType GetChainType() const { return m_chain_type; }
    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector<std::string>& DNSSeeds() const { return vSeeds; }
    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const std::string& Bech32HRP() const { return bech32_hrp; }
    const std::vector<uint8_t>& FixedSeeds() const { return vFixedSeeds; }
    const CCheckpointData& Checkpoints() const { return checkpointData; }

    std::optional<AssumeutxoData> AssumeutxoForHeight(int height) const
    {
        return FindFirst(m_assumeutxo_data, [&](const auto& d) { return d.height == height; });
    }
    std::optional<AssumeutxoData> AssumeutxoForBlockhash(const uint256& blockhash) const
    {
        return FindFirst(m_assumeutxo_data, [&](const auto& d) { return d.blockhash == blockhash; });
    }

    const ChainTxData& TxData() const { return chainTxData; }

    /* Check whether the given tx is a "historic relic" for which to
       skip the validity check.  Return also the "type" of the bug,
       which determines further actions.  */
    /* FIXME: Move to consensus params!  */
    bool IsHistoricBug(const uint256& txid, unsigned nHeight, BugType& type) const;
    virtual ~CChainParams() {}

    /**
     * SigNetOptions holds configurations for creating a signet CChainParams.
     */
    struct SigNetOptions {
        std::optional<std::vector<uint8_t>> challenge{};
        std::optional<std::vector<std::string>> seeds{};
    };

    /**
     * VersionBitsParameters holds activation parameters
     */
    struct VersionBitsParameters {
        int64_t start_time;
        int64_t timeout;
        int min_activation_height;
    };

    /**
     * RegTestOptions holds configurations for creating a regtest CChainParams.
     */
    struct RegTestOptions {
        std::unordered_map<Consensus::DeploymentPos, VersionBitsParameters> version_bits_parameters{};
        std::unordered_map<Consensus::BuriedDeployment, int> activation_heights{};
        bool fastprune{false};
    };

    static std::unique_ptr<const CChainParams> RegTest(const RegTestOptions& options);
    static std::unique_ptr<const CChainParams> SigNet(const SigNetOptions& options);
    static std::unique_ptr<const CChainParams> Main();
    static std::unique_ptr<const CChainParams> TestNet();
    static std::unique_ptr<const CChainParams> TestNet4();

protected:
    CChainParams() = default;

    Consensus::Params consensus;
    MessageStartChars pchMessageStart;
    uint16_t nDefaultPort;
    uint64_t nPruneAfterHeight;
    uint64_t m_assumed_blockchain_size;
    uint64_t m_assumed_chain_state_size;
    std::vector<std::string> vSeeds;
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    std::string bech32_hrp;
    ChainType m_chain_type;
    CBlock genesis;
    std::vector<uint8_t> vFixedSeeds;
    bool fDefaultConsistencyChecks;
    bool m_is_mockable_chain;
    CCheckpointData checkpointData;
    std::vector<AssumeutxoData> m_assumeutxo_data;
    ChainTxData chainTxData;

    /* Map (block height, txid) pairs for buggy transactions onto their
       bug type value.  */
    std::map<std::pair<unsigned, uint256>, BugType> mapHistoricBugs;

    /* Utility routine to insert into historic bug map.  */
    inline void addBug(unsigned nHeight, const char* txid, BugType type)
    {
        std::pair<unsigned, uint256> key(nHeight, uint256S(txid));
        mapHistoricBugs.insert(std::make_pair(key, type));
    }
};

std::optional<ChainType> GetNetworkForMagic(const MessageStartChars& pchMessageStart);

#endif // BITCOIN_KERNEL_CHAINPARAMS_H
