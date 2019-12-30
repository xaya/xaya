// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/mining.h>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <key_io.h>
#include <miner.h>
#include <pow.h>
#include <powdata.h>
#include <script/standard.h>
#include <validation.h>

CTxIn generatetoaddress(const std::string& address)
{
    const auto dest = DecodeDestination(address);
    assert(IsValidDestination(dest));
    const auto coinbase_script = GetScriptForDestination(dest);

    return MineBlock(coinbase_script);
}

CTxIn MineBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(coinbase_scriptPubKey);

    auto& fakeHeader = block->pow.initFakeHeader (*block);
    while (!block->pow.checkProofOfWork(fakeHeader, Params().GetConsensus())) {
        ++fakeHeader.nNonce;
        assert(fakeHeader.nNonce);
    }

    bool processed{ProcessNewBlock(Params(), block, true, nullptr)};
    assert(processed);

    return CTxIn{block->vtx[0]->GetHash(), 0};
}

std::shared_ptr<CBlock> PrepareBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = std::make_shared<CBlock>(
        BlockAssembler{Params()}
            .CreateNewBlock(PowAlgo::NEOSCRYPT, coinbase_scriptPubKey)
            ->block);

    LOCK(cs_main);
    block->nTime = ::ChainActive().Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}
