// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <hash.h>
#include <kernel/messagestartchars.h>
#include <logging.h>
#include <powdata.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace
{

constexpr const char pszTimestampTestnet[] = "Decentralised Autonomous Worlds";
constexpr const char pszTimestampMainnet[]
    = "HUC #2,351,800: "
      "8730ea650d24cd01692a5adb943e7b8720b0ba8a4c64ffcdf5a95d9b3fb57b7f";

/* Premined amount is 222,222,222 CHI.  This is the maximum possible number of
   coins needed in case everything is sold in the ICO.  If this is not the case
   and we need to reduce the coin supply, excessive coins will be burnt by
   sending to an unspendable OP_RETURN output.  */
constexpr CAmount premineAmount = 222222222 * COIN;

/*
The premine on regtest is sent to a 1-of-2 multisig address.

The two addresses and corresponding privkeys are:
  cRH94YMZVk4MnRwPqRVebkLWerCPJDrXGN:
    b69iyynFSWcU54LqXisbbqZ8uTJ7Dawk3V3yhht6ykxgttqMQFjb
  ceREF8QnXPsJ2iVQ1M4emggoXiXEynm59D:
    b3fgAKVQpMj24gbuh6DiXVwCCjCbo1cWiZC2fXgWEU9nXy6sdxD5

This results in the multisig address: dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p
Redeem script:
  512103c278d06b977e67b8ea45ef24e3c96a9258c47bc4cce3d0b497b690d672497b6e21
  0221ac9dc97fe12a98374344d08b458a9c2c1df9afb29dd6089b94a3b4dc9ad57052ae

The constant below is the HASH160 of the redeem script.  In other words, the
final premine script will be:
  OP_HASH160 hexPremineAddress OP_EQUAL
*/
constexpr const char hexPremineAddressRegtest[]
    = "2b6defe41aa3aa47795b702c893c73e716d485ab";

/*
The premine on testnet and mainnet is sent to a 2-of-4 multisig address.  The
keys are held by the founding members of the Xaya team.

The address is:
  DHy2615XKevE23LVRVZVxGeqxadRGyiFW4

The hash of the redeem script is the constant below.  With it, the final
premine script is:
  OP_HASH160 hexPremineAddress OP_EQUAL
*/
constexpr const char hexPremineAddressMainnet[]
    = "8cb1c236d34c74221fe4163bbba739b52e95f484";

CBlock CreateGenesisBlock(const CScript& genesisInputScript, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = genesisInputScript;
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = 0;
    genesis.nNonce   = 0;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);

    std::unique_ptr<CPureBlockHeader> fakeHeader(new CPureBlockHeader ());
    fakeHeader->nNonce = nNonce;
    fakeHeader->hashMerkleRoot = genesis.GetHash ();
    genesis.pow.setCoreAlgo (PowAlgo::NEOSCRYPT);
    genesis.pow.setBits (nBits);
    genesis.pow.setFakeHeader (std::move (fakeHeader));

    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 */
CBlock
CreateGenesisBlock (const uint32_t nTime, const uint32_t nNonce,
                    const uint32_t nBits,
                    const std::string& timestamp,
                    const uint160& premineP2sh)
{
  const std::vector<unsigned char> timestampData(timestamp.begin (),
                                                 timestamp.end ());
  const CScript genesisInput = CScript () << timestampData;

  std::vector<unsigned char>
    scriptHash (premineP2sh.begin (), premineP2sh.end ());
  std::reverse (scriptHash.begin (), scriptHash.end ());
  const CScript genesisOutput = CScript ()
    << OP_HASH160 << scriptHash << OP_EQUAL;

  const int32_t nVersion = 1;
  return CreateGenesisBlock (genesisInput, genesisOutput, nTime, nNonce, nBits,
                             nVersion, premineAmount);
}

/**
 * Mines the genesis block (by finding a suitable nonce only).  When done, it
 * prints the found nonce and block hash and exits.
 */
void MineGenesisBlock (CBlock& block, const Consensus::Params& consensus)
{
  std::cout << "Mining genesis block..." << std::endl;

  block.nTime = GetTime ();

  auto& fakeHeader = block.pow.initFakeHeader (block);
  while (!block.pow.checkProofOfWork (fakeHeader, consensus))
    {
      assert (fakeHeader.nNonce < std::numeric_limits<uint32_t>::max ());
      ++fakeHeader.nNonce;
      if (fakeHeader.nNonce % 1000 == 0)
        std::cout << "  nNonce = " << fakeHeader.nNonce << "..." << std::endl;
    }

  std::cout << "Found nonce: " << fakeHeader.nNonce << std::endl;
  std::cout << "nTime: " << block.nTime << std::endl;
  std::cout << "Block hash: " << block.GetHash ().GetHex () << std::endl;
  std::cout << "Merkle root: " << block.hashMerkleRoot.GetHex () << std::endl;
  exit (EXIT_SUCCESS);
}

} // anonymous namespace

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        m_chain_type = ChainType::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 4200000;
        /* The value of ~3.8 CHI is calculated to yield the desired total
           PoW coin supply.  For the calculation, see here:

           https://github.com/xaya/xaya/issues/70#issuecomment-441292533
        */
        consensus.initialSubsidy = 382934346;
        consensus.BIP16Height = 0;
        consensus.BIP34Height = 1;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 2016; // segwit activation height + miner confirmation window
        consensus.powLimitNeoscrypt = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // The best chain should have at least this much work.
        // The value is the chain work of the Xaya mainnet chain at height
        // 5'650'000, with best block hash:
        // 2a3dc10e67db47a6171d967cd8c11e0c6fed47b6a20280bd1d1e8965ecd28380
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000006ff94414d49b7d0269284df");
        consensus.defaultAssumeValid = uint256S("0x2a3dc10e67db47a6171d967cd8c11e0c6fed47b6a20280bd1d1e8965ecd28380"); // 5'650'000

        consensus.nAuxpowChainId = 1829;

        consensus.rules.reset(new Consensus::MainNetConsensus());

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xcc;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xfe;
        nDefaultPort = 8394;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 6;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock (1531470713, 482087, 0x1e0ffff0,
                                      pszTimestampMainnet,
                                      uint160S (hexPremineAddressMainnet));
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("e5062d76e5f50c42f493826ac9920b63a8def2626fd70a5cec707ec47a4c4651"));
        assert(genesis.hashMerkleRoot == uint256S("0827901b75ab43978c3cf20a78baf040faeb0e2eeff3a2c58ab6521a6d46f8fd"));

        vSeeds.emplace_back("seed.xaya.io.");
        vSeeds.emplace_back("seed.xaya.domob.eu.");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,28);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,30);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,130);
        /* FIXME: Update these below.  */
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "chi";

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {      0, uint256S("ce46f5f898b38e9c8c5e9ae4047ef5bccc42ec8eca0142202813a625e6dc2656")},
                { 340000, uint256S("e685ccaa62025c5c5075cfee80e498589bd4788614dcbe397e12bf2b8e887e47")},
                {1234000, uint256S("a853c0581c3637726a769b77cadf185e09666742757ef2df00058e876cf25897")},
            }
        };

        m_assumeutxo_data = {
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 4096 2a3dc10e67db47a6171d967cd8c11e0c6fed47b6a20280bd1d1e8965ecd28380
            .nTime    = 1709586236,
            .nTxCount = 7854923,
            .dTxRate  = 0.05630403402300278,
        };
    }

    int DefaultCheckNameDB () const override
    {
        return -1;
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        m_chain_type = ChainType::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 4200000;
        consensus.initialSubsidy = 10 * COIN;
        consensus.BIP16Height = 0;
        consensus.BIP34Height = 1;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 2016; // segwit activation height + miner confirmation window
        consensus.MinBIP9WarningHeight = consensus.SegwitHeight + consensus.nMinerConfirmationWindow;
        consensus.powLimitNeoscrypt = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // The best chain should have at least this much work.
        // 110'000 with best block hash:
        // 01547d538737e01d81d207e7d2f4c8f2510c6b82f0ee5dd8cd6c26bed5a03d0f
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000e59eda1191b9");
        consensus.defaultAssumeValid = uint256S("0x01547d538737e01d81d207e7d2f4c8f2510c6b82f0ee5dd8cd6c26bed5a03d0f"); // 110'000

        consensus.nAuxpowChainId = 1829;

        consensus.rules.reset(new Consensus::TestNetConsensus());

        pchMessageStart[0] = 0xcc;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xfe;
        nDefaultPort = 18394;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock (1530623291, 343829, 0x1e0ffff0,
                                      pszTimestampTestnet,
                                      uint160S (hexPremineAddressMainnet));
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("5195fc01d0e23d70d1f929f21ec55f47e1c6ea1e66fae98ee44cbbc994509bba"));
        assert(genesis.hashMerkleRoot == uint256S("59d1a23342282179e810dff9238a97d07bd8602e3a1ba0efb5f519008541f257"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.emplace_back("seed.testnet.xaya.io.");
        vSeeds.emplace_back("seed.testnet.xaya.domob.eu.");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,88);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,90);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,230);
        /* FIXME: Update these below.  */
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "chitn";

        // FIXME: Namecoin has no fixed seeds for testnet, so that the line
        // below errors out.  Use it once we have testnet seeds.
        //vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));
        vFixedSeeds.clear();

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {     0, uint256S("3bcc29e821e7fbd374c7460306eb893725d69dbee87c4774cdcd618059b6a578")},
                { 11000, uint256S("57670b799b6645c7776e9fdbd6abff510aaed9790625dd28072d0e87a7fafcf4")},
                { 70000, uint256S("e2c154dc8e223cef271b54174c9d66eaf718378b30977c3df115ded629f3edb1")},
            }
        };

        m_assumeutxo_data = {
            {
                .height = 2'500'000,
                .hash_serialized = AssumeutxoHash{uint256S("0xf841584909f68e47897952345234e37fcd9128cd818f41ee6c3ca68db8071be7")},
                .nChainTx = 66484552,
                .blockhash = uint256S("0x0000000000000093bcb68c03a9a168ae252572d348a2eaeba2cdf9231d73206f")
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 01547d538737e01d81d207e7d2f4c8f2510c6b82f0ee5dd8cd6c26bed5a03d0f
            .nTime    = 1586091497,
            .nTxCount = 113579,
            .dTxRate  = 0.002815363095612851,
        };
    }

    int DefaultCheckNameDB () const override
    {
        return -1;
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        if (!options.challenge) {
            /* FIXME: Adjust the default signet challenge to something else if
               we want to use signet for Namecoin.  */
            bin = ParseHex("512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae");
            //vSeeds.emplace_back("178.128.221.177");

            consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000000206e86f08e8");
            consensus.defaultAssumeValid = uint256S("0x0000000870f15246ba23c16e370a7ffb1fc8a3dcf8cb4492882ed4b0e3d4cd26"); // 180000
            m_assumed_blockchain_size = 1;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 0000000870f15246ba23c16e370a7ffb1fc8a3dcf8cb4492882ed4b0e3d4cd26
                .nTime    = 1706331472,
                .nTxCount = 2425380,
                .dTxRate  = 0.008277759863833788,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        m_chain_type = ChainType::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Height = 1;
        consensus.BIP34Height = 1;
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimitNeoscrypt = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nAuxpowChainId = 1829;

        consensus.rules.reset(new Consensus::TestNetConsensus());

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 38394;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock (1601286749, 534547, 0x1e0ffff0,
                                      pszTimestampTestnet,
                                      uint160S (hexPremineAddressMainnet));
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x8d5223e215a03970bb3d3bc511a0d9a003e03cbc973289611ca6e0e617f57ccf"));
        assert(genesis.hashMerkleRoot == uint256S("0x59d1a23342282179e810dff9238a97d07bd8602e3a1ba0efb5f519008541f257"));

        vFixedSeeds.clear();

        m_assumeutxo_data = {
            {
                .height = 160'000,
                .hash_serialized = AssumeutxoHash{uint256S("0xfe0a44309b74d6b5883d246cb419c6221bcccf0b308c9b59b7d70783dbdf928a")},
                .nChainTx = 2289496,
                .blockhash = uint256S("0x0000003ca3c99aff040f2563c2ad8f8ec88bd0fd6b8f0895cfaf1ef90353a62c")
            }
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,88);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,90);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,230);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;
    }

    int DefaultCheckNameDB () const override
    {
        return -1;
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        // The subsidy for regtest net is kept same as upstream Bitcoin, so
        // that we don't have to update many of the tests unnecessarily.
        consensus.initialSubsidy = 50 * COIN;
        consensus.BIP16Height = 0;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimitNeoscrypt = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        consensus.nAuxpowChainId = 1829;

        consensus.rules.reset(new Consensus::RegTestConsensus());

        pchMessageStart[0] = 0xcc;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 18495;
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_P2SH:
                consensus.BIP16Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        genesis = CreateGenesisBlock (1300000000, 0, 0x207fffff,
                                      pszTimestampTestnet,
                                      uint160S (hexPremineAddressRegtest));
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("6f750b36d22f1dc3d0a6e483af45301022646dfc3b3ba2187865f5a7d6d83ab1"));
        assert(genesis.hashMerkleRoot == uint256S("9f96a4c275320aaf6386652444be5baade11e2f9f40221a98b968ae5c32dd55a"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        checkpointData = {
            {
                {0, uint256S("18042820e8a9f538e77e93c500768e5be76720383cd17e9b419916d8f356c619")},
            }
        };

        m_assumeutxo_data = {
            {
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256S("0xc7b1cf5103d6dd47a4feddb01f0fc951d109ed88f9b406f720a8a7f9942689e4")},
                .nChainTx = 111,
                .blockhash = uint256S("0xb5b31111b3ee8c91956ffb9b248950dd26a878eb72ab7d9e9286bb27603c1ba2")
            },
            {
                // For use by test/functional/feature_assumeutxo.py
                .height = 299,
                .hash_serialized = AssumeutxoHash{uint256S("0xbc222dd2a08a561ff47d77c06af1fe35127bf4840392a83475332f45ea5efa3e")},
                .nChainTx = 334,
                .blockhash = uint256S("0xcb3e6696a6e1713994cf6daf8c0c874e51d04a9f7ef5a19595639f0293002f70")
            },
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,88);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,90);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,230);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "chirt";
    }

    int DefaultCheckNameDB () const override
    {
        return 0;
    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
}
