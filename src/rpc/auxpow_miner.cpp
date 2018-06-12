// Copyright (c) 2018 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/auxpow_miner.h>

#include <arith_uint256.h>
#include <auxpow.h>
#include <chainparams.h>
#include <net.h>
#include <rpc/protocol.h>
#include <utilstrencodings.h>
#include <utiltime.h>
#include <validation.h>

#include <cassert>

namespace
{

void auxMiningCheck()
{
  if (!g_connman)
    throw JSONRPCError (RPC_CLIENT_P2P_DISABLED,
                        "Error: Peer-to-peer functionality missing or"
                        " disabled");

  if (g_connman->GetNodeCount (CConnman::CONNECTIONS_ALL) == 0
        && !Params ().MineBlocksOnDemand ())
    throw JSONRPCError (RPC_CLIENT_NOT_CONNECTED,
                        "Chimaera is not connected!");

  if (IsInitialBlockDownload () && !Params ().MineBlocksOnDemand ())
    throw JSONRPCError (RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                        "Chimaera is downloading blocks...");
}

}  // anonymous namespace

const CBlock*
AuxpowMiner::getCurrentBlock (const CScript& scriptPubKey, uint256& target)
{
  AssertLockHeld (cs);

  {
    LOCK (cs_main);
    if (pindexPrev != chainActive.Tip ()
        || (mempool.GetTransactionsUpdated () != txUpdatedLast
            && GetTime () - startTime > 60))
      {
        if (pindexPrev != chainActive.Tip ())
          {
            /* Clear old blocks since they're obsolete now.  */
            blocks.clear ();
            templates.clear ();
            pblockCur = nullptr;
          }

        /* Create new block with nonce = 0 and extraNonce = 1.  */
        std::unique_ptr<CBlockTemplate> newBlock
            = BlockAssembler (Params ()).CreateNewBlock (scriptPubKey);
        if (newBlock == nullptr)
          throw JSONRPCError (RPC_OUT_OF_MEMORY, "out of memory");

        /* Update state only when CreateNewBlock succeeded.  */
        txUpdatedLast = mempool.GetTransactionsUpdated ();
        pindexPrev = chainActive.Tip ();
        startTime = GetTime ();

        /* Finalise it by building the merkle root.  */
        IncrementExtraNonce (&newBlock->block, pindexPrev, extraNonce);

        /* Save in our map of constructed blocks.  */
        pblockCur = &newBlock->block;
        blocks[pblockCur->GetHash ()] = pblockCur;
        templates.push_back (std::move (newBlock));
      }
  }

  /* At this point, pblockCur is always initialised:  If we make it here
     without creating a new block above, it means that, in particular,
     pindexPrev == chainActive.Tip().  But for that to happen, we must
     already have created a pblockCur in a previous call, as pindexPrev is
     initialised only when pblockCur is.  */
  assert (pblockCur);

  arith_uint256 arithTarget;
  bool fNegative, fOverflow;
  arithTarget.SetCompact (pblockCur->nBits, &fNegative, &fOverflow);
  if (fNegative || fOverflow || arithTarget == 0)
    throw std::runtime_error ("invalid difficulty bits in block");
  target = ArithToUint256 (arithTarget);

  return pblockCur;
}

const CBlock*
AuxpowMiner::lookupSavedBlock (const std::string& hashHex) const
{
  AssertLockHeld (cs);

  uint256 hash;
  hash.SetHex (hashHex);

  const auto iter = blocks.find (hash);
  if (iter == blocks.end ())
    throw JSONRPCError (RPC_INVALID_PARAMETER, "block hash unknown");

  return iter->second;
}

UniValue
AuxpowMiner::createAuxBlock (const CScript& scriptPubKey)
{
  auxMiningCheck ();
  LOCK (cs);

  uint256 target;
  const CBlock* pblock = getCurrentBlock (scriptPubKey, target);

  UniValue result(UniValue::VOBJ);
  result.pushKV ("hash", pblock->GetHash ().GetHex ());
  result.pushKV ("algo", "sha256d");
  result.pushKV ("chainid", Params ().GetConsensus ().nAuxpowChainId);
  result.pushKV ("previousblockhash", pblock->hashPrevBlock.GetHex ());
  result.pushKV ("coinbasevalue",
                 static_cast<int64_t> (pblock->vtx[0]->vout[0].nValue));
  result.pushKV ("bits", strprintf ("%08x", pblock->nBits));
  result.pushKV ("height", static_cast<int64_t> (pindexPrev->nHeight + 1));
  result.pushKV ("_target", HexStr (BEGIN (target), END (target)));

  return result;
}

bool
AuxpowMiner::submitAuxBlock (const std::string& hashHex,
                             const std::string& auxpowHex) const
{
  auxMiningCheck ();

  std::shared_ptr<CBlock> shared_block;
  {
    LOCK (cs);
    const CBlock* pblock = lookupSavedBlock (hashHex);
    shared_block = std::make_shared<CBlock> (*pblock);
  }

  const std::vector<unsigned char> vchAuxPow = ParseHex (auxpowHex);
  CDataStream ss(vchAuxPow, SER_GETHASH, PROTOCOL_VERSION);
  CAuxPow pow;
  ss >> pow;
  // FIXME: Enable once the block format is actually changed to allow for
  // external PoW data.
  //shared_block->SetAuxpow (new CAuxPow (pow));
  assert (shared_block->GetHash ().GetHex () == hashHex);

  return ProcessNewBlock (Params (), shared_block, true, nullptr);
}
