// Copyright (c) 2018 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/auxpow_miner.h>

#include <arith_uint256.h>
#include <auxpow.h>
#include <chainparams.h>
#include <net.h>
#include <primitives/pureheader.h>
#include <rpc/protocol.h>
#include <streams.h>
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
AuxpowMiner::getCurrentBlock (const PowAlgo algo, const CScript& scriptPubKey,
                              uint256& target)
{
  AssertLockHeld (cs);

  {
    LOCK (cs_main);
    if (pindexPrev != chainActive.Tip ()
        || pblockCur->pow.getCoreAlgo () != algo
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
            = BlockAssembler (Params ()).CreateNewBlock (algo, scriptPubKey);
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
  arithTarget.SetCompact (pblockCur->pow.getBits (), &fNegative, &fOverflow);
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
  const CBlock* pblock = getCurrentBlock (PowAlgo::SHA256D, scriptPubKey,
                                          target);

  UniValue result(UniValue::VOBJ);
  result.pushKV ("hash", pblock->GetHash ().GetHex ());
  result.pushKV ("algo", "sha256d");
  result.pushKV ("chainid", Params ().GetConsensus ().nAuxpowChainId);
  result.pushKV ("previousblockhash", pblock->hashPrevBlock.GetHex ());
  result.pushKV ("coinbasevalue",
                 static_cast<int64_t> (pblock->vtx[0]->vout[0].nValue));
  result.pushKV ("bits", strprintf ("%08x", pblock->pow.getBits ()));
  result.pushKV ("height", static_cast<int64_t> (pindexPrev->nHeight + 1));
  result.pushKV ("_target", HexStr (BEGIN (target), END (target)));

  return result;
}

namespace
{

// Copied from the diff in https://github.com/bitcoin/bitcoin/pull/4100.
int
FormatHashBlocks(void* pbuffer, unsigned int len)
{
    unsigned char* pdata = (unsigned char*)pbuffer;
    unsigned int blocks = 1 + ((len + 8) / 64);
    unsigned char* pend = pdata + 64 * blocks;
    memset(pdata + len, 0, 64 * blocks - len);
    pdata[len] = 0x80;
    unsigned int bits = len * 8;
    pend[-1] = (bits >> 0) & 0xff;
    pend[-2] = (bits >> 8) & 0xff;
    pend[-3] = (bits >> 16) & 0xff;
    pend[-4] = (bits >> 24) & 0xff;
    return blocks;
}

}  // anonymous namespace

UniValue
AuxpowMiner::createWork (const CScript& scriptPubKey)
{
  auxMiningCheck ();
  LOCK (cs);

  uint256 target;
  const CBlock* pblock = getCurrentBlock (PowAlgo::NEOSCRYPT, scriptPubKey,
                                          target);

  CPureBlockHeader fakeHeader;
  fakeHeader.SetNull ();
  fakeHeader.hashMerkleRoot = pblock->GetHash ();

  /* To construct the data result, we first have to serialise the template
     fake header of the PoW data.  Then perform the byte-order swapping and
     add zero-padding up to 128 bytes.  */
  std::vector<unsigned char> data;
  CVectorWriter writer(SER_GETHASH, PROTOCOL_VERSION, data, 0);
  writer << fakeHeader;
  const size_t len = data.size ();
  data.resize (128, 0);
  FormatHashBlocks (&data[0], len);
  SwapGetWorkEndianness (data);

  UniValue result(UniValue::VOBJ);
  result.pushKV ("hash", pblock->GetHash ().GetHex ());
  result.pushKV ("data", HexStr (data.begin (), data.end ()));
  result.pushKV ("algo", "neoscrypt");
  result.pushKV ("previousblockhash", pblock->hashPrevBlock.GetHex ());
  result.pushKV ("coinbasevalue",
                 static_cast<int64_t> (pblock->vtx[0]->vout[0].nValue));
  result.pushKV ("bits", strprintf ("%08x", pblock->pow.getBits ()));
  result.pushKV ("height", static_cast<int64_t> (pindexPrev->nHeight + 1));
  result.pushKV ("target", HexStr (BEGIN (target), END (target)));

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
  std::unique_ptr<CAuxPow> pow(new CAuxPow ());
  ss >> *pow;
  // FIXME: Enable once the block format is actually changed to allow for
  // external PoW data.
  //shared_block->SetAuxpow (std::move (pow));
  assert (shared_block->GetHash ().GetHex () == hashHex);

  return ProcessNewBlock (Params (), shared_block, true, nullptr);
}

bool
AuxpowMiner::submitWork (const std::string& hashHex,
                         const std::string& dataHex) const
{
  auxMiningCheck ();

  std::shared_ptr<CBlock> shared_block;
  {
    LOCK (cs);
    const CBlock* pblock = lookupSavedBlock (hashHex);
    shared_block = std::make_shared<CBlock> (*pblock);
  }

  std::vector<unsigned char> vchData = ParseHex (dataHex);
  if (vchData.size () < 80)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid size of data");
  vchData.resize (80);
  SwapGetWorkEndianness (vchData);

  CDataStream ss(vchData, SER_GETHASH, PROTOCOL_VERSION);
  std::unique_ptr<CPureBlockHeader> fakeHeader(new CPureBlockHeader ());
  ss >> *fakeHeader;

  shared_block->pow.setFakeHeader (std::move (fakeHeader));
  assert (shared_block->GetHash ().GetHex () == hashHex);

  return ProcessNewBlock (Params (), shared_block, true, nullptr);
}
