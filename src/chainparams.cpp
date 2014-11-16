// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"

#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

using namespace std;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

bool CChainParams::IsHistoricBug(const uint256& txid, unsigned nHeight, BugType& type) const
{
    const std::pair<unsigned, uint256> key(nHeight, txid);
    std::map<std::pair<unsigned, uint256>, BugType>::const_iterator mi;

    mi = mapHistoricBugs.find (key);
    if (mi != mapHistoricBugs.end ())
    {
        type = mi->second;
        return true;
    }

    return false;
}

/**
 * Main network
 */

//! Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int i = 0; i < count; i++)
    {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */
static Checkpoints::MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        (  2016, uint256S("0x0000000000660bad0d9fbde55ba7ee14ddf766ed5f527e3fbca523ac11460b92"))
        (  4032, uint256S("0x0000000000493b5696ad482deb79da835fe2385304b841beef1938655ddbc411"))
        (  6048, uint256S("0x000000000027939a2e1d8bb63f36c47da858e56d570f143e67e85068943470c9"))
        (  8064, uint256S("0x000000000003a01f708da7396e54d081701ea406ed163e519589717d8b7c95a5"))
        ( 10080, uint256S("0x0x00000000000fed3899f818b2228b4f01b9a0a7eeee907abd172852df71c64b06"))
        ( 12096, uint256S("0x0000000000006c06988ff361f124314f9f4bb45b6997d90a7ee4cedf434c670f"))
        ( 14112, uint256S("0x00000000000045d95e0588c47c17d593c7b5cb4fb1e56213d1b3843c1773df2b"))
        ( 16128, uint256S("0x000000000001d9964f9483f9096cf9d6c6c2886ed1e5dec95ad2aeec3ce72fa9"))
        ( 18940, uint256S("0x00000000000087f7fc0c8085217503ba86f796fa4984f7e5a08b6c4c12906c05"))
        /* FIXME: Add more checkpoints!  */
        ;
/* FIXME: update checkpoint data.  */
static const Checkpoints::CCheckpointData data = {
        &mapCheckpoints,
        1397080064, // * UNIX timestamp of last checkpoint block
        36544669,   // * total number of transactions between genesis and last checkpoint
                    //   (the tx=... number in the SetBestChain debug.log lines)
        60000.0     // * estimated number of transactions per day after checkpoint
    };

/* FIXME: Update.  */
static Checkpoints::MapCheckpoints mapCheckpointsTestnet =
        boost::assign::map_list_of
        ( 546, uint256S("000000002a936ca763904c3c35fce2f3556c559c0214345d31b1bcebf76acb70"))
        ;
static const Checkpoints::CCheckpointData dataTestnet = {
        &mapCheckpointsTestnet,
        1337966069,
        1488,
        300
    };

static Checkpoints::MapCheckpoints mapCheckpointsRegtest =
        boost::assign::map_list_of
        ( 0, uint256S("5287b3809b71433729402429b7d909a853cfac5ed40f09117b242c275e6b2d63"))
        ;
static const Checkpoints::CCheckpointData dataRegtest = {
        &mapCheckpointsRegtest,
        0,
        0,
        0
    };

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        /** 
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xfe;
        vAlertPubKey = ParseHex("04ba207043c1575208f08ea6ac27ed2aedd4f84e70b874db129acb08e6109a3bbb7c479ae22565973ebf0ac0391514511a22cb9345bdb772be20cfbd38be578b0c");
        nDefaultPort = 8334;
        bnProofOfWorkLimit = ~arith_uint256(0) >> 32;
        nSubsidyHalvingInterval = 210000;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 0;
        nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        nTargetSpacing = 10 * 60;

        /**
         * Build the genesis block. Note that the output of the genesis coinbase cannot
         * be spent as it did not originally exist in the database.
         * 
         * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
         *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
         *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
         *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
         *   vMerkleTree: 4a5e1e
         */
        const char* pszTimestamp = "... choose what comes next.  Lives of your own, or a return to chains. -- V";
        CMutableTransaction txNew;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 0x1c007fff << CScriptNum(522) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 50 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("04b620369050cd899ffbbc4e8ee51e8c4534a855bb463439d63d235d4779685d8b6f4870a238cf365ac94fa13ef9a2a22cd99d0d5ee86dcabcafce36c7acf43ce5") << OP_CHECKSIG;
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock.SetNull();
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion.SetGenesisVersion(1);
        genesis.nTime    = 1303000001;
        genesis.nBits    = 0x1c007fff;
        genesis.nNonce   = 0xa21ea192U;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256S("0x000000000062b72c5e2ceb45fbc8587e807c155b0da735e6483dfba2f0a9c770"));
        assert(genesis.hashMerkleRoot == uint256S("0x41c62dbd9068c89a449525e3cd5ac61b20ece28c3c38b3f35b2161f0e6d3cb0d"));

        /* FIXME: DNS seeds?  */
        //vSeeds.push_back(CDNSSeedData("bitcoin.sipa.be", "seed.bitcoin.sipa.be"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,52);
        /* FIXME: What are these below?  */
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fDefaultCheckMemPool = false;
        fAllowMinDifficultyBlocks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fSkipProofOfWorkCheck = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        /* These transactions have name outputs but a non-Namecoin tx version.
           They contain NAME_NEWs, which are fine, and also NAME_FIRSTUPDATE.
           The latter are not interpreted by namecoind, thus also ignore
           them for us here.  *Just* NAME_NEW outputs are handled by
           special "lenient version checks".  Below are only those transactions
           that also have name registrations.  */
        addBug(98423, "bff3ed6873e5698b97bf0c28c29302b59588590b747787c7d1ef32decdabe0d1", BUG_FULLY_IGNORE);
        addBug(98424, "e9b211007e5cac471769212ca0f47bb066b81966a8e541d44acf0f8a1bd24976", BUG_FULLY_IGNORE);
        addBug(98425, "8aa2b0fc7d1033de28e0192526765a72e9df0c635f7305bdc57cb451ed01a4ca", BUG_FULLY_IGNORE);

        /* This transaction has both a NAME_NEW and a NAME_FIRSTUPDATE as
           inputs.  It is fine to accept it as valid and just process the
           NAME_FIRSTUPDATE.  (NAME_NEW has no special side-effect in applying
           anyway.)  */
        addBug(99381, "774d4c446cecfc40b1c02fdc5a13be6d2007233f9d91daefab6b3c2e70042f05", BUG_FULLY_APPLY);

        /* These were libcoin's name stealing bugs.  */
        addBug(139872, "2f034f2499c136a2c5a922ca4be65c1292815c753bbb100a2a26d5ad532c3919", BUG_IN_UTXO);
        addBug(139936, "c3e76d5384139228221cce60250397d1b87adf7366086bc8d6b5e6eee03c55c7", BUG_FULLY_IGNORE);
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return data;
    }

    int AuxpowStartHeight() const
    {
        return 19200;
    }

    bool StrictChainId() const
    {
        return true;
    }

    bool AllowLegacyBlocks(unsigned nHeight) const
    {
        return static_cast<int> (nHeight) < AuxpowStartHeight();
    }

    unsigned NameExpirationDepth (unsigned nHeight) const
    {
        /* Important:  It is assumed (in ExpireNames) that
           "n - expirationDepth(n)" is increasing!  (This is
           the update height up to which names expire at height n.)  */

        if (nHeight < 24000)
            return 12000;
        if (nHeight < 48000)
            return nHeight - 12000;

        return 36000;
    }

    bool LenientVersionCheck(unsigned) const
    {
        /* FIXME: Update after soft fork.  */
        return true;
    }

    int DefaultCheckNameDB () const
    {
        return -1;
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
/* FIXME: Update for Namecoin.  */
class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x09;
        pchMessageStart[3] = 0x07;
        vAlertPubKey = ParseHex("04302390343f91cc401d56d68b123028bf52e5fca1939df127f63c6467cdf9c8e2c14b61104cf817d0b780da337893ecc4aaff1309e536162dabbdb45200ca2b0a");
        nDefaultPort = 18334;
        nEnforceBlockUpgradeMajority = 51;
        nRejectBlockOutdatedMajority = 75;
        nToCheckBlockUpgradeMajority = 100;
        nMinerThreads = 0;
        nTargetTimespan = 14 * 24 * 60 * 60; //! two weeks
        nTargetSpacing = 10 * 60;

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nTime = 1296688602;
        genesis.nNonce = 414098458;
        hashGenesisBlock = genesis.GetHash();
        //assert(hashGenesisBlock == uint256S("0x000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("alexykot.me", "testnet-seed.alexykot.me"));
        vSeeds.push_back(CDNSSeedData("bitcoin.petertodd.org", "testnet-seed.bitcoin.petertodd.org"));
        vSeeds.push_back(CDNSSeedData("bluematt.me", "testnet-seed.bluematt.me"));
        vSeeds.push_back(CDNSSeedData("bitcoin.schildbach.de", "testnet-seed.bitcoin.schildbach.de"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fDefaultCheckMemPool = false;
        fAllowMinDifficultyBlocks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        mapHistoricBugs.clear();
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataTestnet;
    }

    int AuxpowStartHeight() const
    {
        return 0;
    }

    bool StrictChainId() const
    {
        return false;
    }

    bool AllowLegacyBlocks(unsigned) const
    {
        return true;
    }

    bool LenientVersionCheck(unsigned) const
    {
        return false;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nSubsidyHalvingInterval = 150;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 1;
        nTargetTimespan = 14 * 24 * 60 * 60; //! two weeks
        nTargetSpacing = 10 * 60;
        bnProofOfWorkLimit = ~arith_uint256(0) >> 1;
        genesis.nTime = 1296688602;
        genesis.nBits = 0x207fffff;
        genesis.nNonce = 2;
        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 18444;
        assert(hashGenesisBlock == uint256S("0x5287b3809b71433729402429b7d909a853cfac5ed40f09117b242c275e6b2d63"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fDefaultCheckMemPool = true;
        fAllowMinDifficultyBlocks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataRegtest;
    }

    bool StrictChainId() const
    {
        return true;
    }

    bool AllowLegacyBlocks(unsigned) const
    {
        return false;
    }

    unsigned NameExpirationDepth (unsigned nHeight) const
    {
        return 30;
    }

    int DefaultCheckNameDB () const
    {
        return 0;
    }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams {
public:
    CUnitTestParams() {
        strNetworkID = "unittest";
        nDefaultPort = 18445;
        vFixedSeeds.clear(); //! Unit test mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Unit test mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fDefaultCheckMemPool = true;
        fAllowMinDifficultyBlocks = false;
        fMineBlocksOnDemand = true;

        mapHistoricBugs.clear();
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        // UnitTest share the same checkpoints as MAIN
        return data;
    }

    /* StrictChainId() = true inherited from CMainParams.  */

    bool AllowLegacyBlocks(unsigned) const
    {
        /* Allow legacy blocks.  This is to make the unit tests that
           rely on loading predefined block data work.  (In particular,
           miner_tests.cpp.)  */
        return true;
    }

    bool LenientVersionCheck(unsigned) const
    {
        return false;
    }

    //! Published setters to allow changing values in unit test cases
    virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval)  { nSubsidyHalvingInterval=anSubsidyHalvingInterval; }
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority)  { nEnforceBlockUpgradeMajority=anEnforceBlockUpgradeMajority; }
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority)  { nRejectBlockOutdatedMajority=anRejectBlockOutdatedMajority; }
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority)  { nToCheckBlockUpgradeMajority=anToCheckBlockUpgradeMajority; }
    virtual void setDefaultCheckMemPool(bool afDefaultCheckMemPool)  { fDefaultCheckMemPool=afDefaultCheckMemPool; }
    virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) {  fAllowMinDifficultyBlocks=afAllowMinDifficultyBlocks; }
    virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
    virtual void setProofOfWorkLimit(const arith_uint256& limit) { bnProofOfWorkLimit = limit; }
};
static CUnitTestParams unitTestParams;


static CChainParams *pCurrentParams = 0;

CModifiableParams *ModifiableParams()
{
   assert(pCurrentParams);
   assert(pCurrentParams==&unitTestParams);
   return (CModifiableParams*)&unitTestParams;
}

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(CBaseChainParams::Network network) {
    switch (network) {
        case CBaseChainParams::MAIN:
            return mainParams;
        case CBaseChainParams::TESTNET:
            return testNetParams;
        case CBaseChainParams::REGTEST:
            return regTestParams;
        case CBaseChainParams::UNITTEST:
            return unitTestParams;
        default:
            assert(false && "Unimplemented network");
            return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
