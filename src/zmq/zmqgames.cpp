// Copyright (c) 2018 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <zmq/zmqgames.h>

#include <amount.h>
#include <chain.h>
#include <core_io.h>
#include <key_io.h>
#include <logging.h>
#include <names/common.h>
#include <names/encoding.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/names.h>
#include <script/standard.h>

#include <univalue.h>

#include <map>

const char* ZMQGameBlocksNotifier::PREFIX_ATTACH = "game-block-attach";
const char* ZMQGameBlocksNotifier::PREFIX_DETACH = "game-block-detach";

bool
ZMQGameBlocksNotifier::SendMessage (const std::string& command,
                                    const UniValue& data)
{
  const std::string dataStr = data.write ();
  return CZMQAbstractPublishNotifier::SendMessage (
      command.c_str (), dataStr.c_str (), dataStr.size ());
}

namespace
{

/**
 * Converts a transaction to the corresponding JSON data that is returned
 * for it in the Xaya game interface.  The returned map contains the data
 * for each of the games that may be referenced (and are tracked).  If the
 * transaction is not a name operation, then the empty map is returned.
 */
std::map<std::string, UniValue>
JsonDataForMove (const CTransaction& tx)
{
  /* Determine if this is a name update at all; if it isn't, then there
     is nothing to do for this transaction.  */
  CNameScript nameOp;
  for (const auto& out : tx.vout)
    {
      nameOp = CNameScript (out.scriptPubKey);
      if (nameOp.isNameOp ())
        break;
    }
  if (!nameOp.isNameOp () || !nameOp.isAnyUpdate ())
    return {};

  /* Only consider updates to p/ names.  */
  const std::string name = EncodeName (nameOp.getOpName (), NameEncoding::UTF8);
  if (name.substr (0, 2) != "p/")
    return {};

  /* See if there are actually games mentioned in the update's value.  */
  const std::string valueStr = EncodeName (nameOp.getOpValue (),
                                           NameEncoding::UTF8);
  UniValue value;
  if (!value.read (valueStr) || !value.isObject ())
    {
      /* This shouldn't actually happen, as the consensus rules check for
         these conditions for name updates.  But if it does happen, we just
         ignore it for here.  */
      LogPrintf ("%s: invalid value ignored\n", __func__);
      return {};
    }
  if (!value.exists ("g"))
    return {};
  const UniValue& g = value["g"];
  if (!g.isObject () || g.empty ())
    return {};

  /* Prepare a template object that is the same for all games.  */
  UniValue tmpl(UniValue::VOBJ);
  tmpl.pushKV ("txid", tx.GetHash ().GetHex ());
  tmpl.pushKV ("name", name.substr (2));

  std::map<std::string, CAmount> outAmounts;
  for (const auto& out : tx.vout)
    {
      const CNameScript nameOp(out.scriptPubKey);
      if (nameOp.isNameOp ())
        continue;

      CTxDestination dest;
      if (!ExtractDestination (out.scriptPubKey, dest))
        continue;

      const std::string addr = EncodeDestination (dest);
      outAmounts[addr] += out.nValue;
    }

  UniValue out(UniValue::VOBJ);
  for (const auto& entry : outAmounts)
    out.pushKV (entry.first, ValueFromAmount (entry.second));
  tmpl.pushKV ("out", out);

  /* Fill the per-game moves into the template.  */
  std::map<std::string, UniValue> result;
  for (const auto& game : g.getKeys ())
    {
      UniValue obj = tmpl;
      obj.pushKV ("move", g[game]);
      result[game] = obj;
    }

  return result;
}

} // anonymous namespace

bool
ZMQGameBlocksNotifier::SendBlockNotifications (
    const std::set<std::string>& games, const std::string& commandPrefix,
    const std::string& reqtoken, const CBlock& block, const CBlockIndex* pindex)
{
  /* Start with an empty array of moves for each game that we track.  */
  std::map<std::string, UniValue> perGameMoves;
  for (const auto& game : games)
    perGameMoves[game] = UniValue (UniValue::VARR);

  /* Add relevant moves for each game from all the transactions.  */
  for (const auto& tx : block.vtx)
    {
      const auto perGameThisTx = JsonDataForMove (*tx);
      for (const auto& entry : perGameThisTx)
        {
          auto mit = perGameMoves.find (entry.first);
          if (mit == perGameMoves.end ())
            continue;

          assert (games.count (entry.first) > 0);
          assert (mit->second.isArray ());
          mit->second.push_back (entry.second);
        }
    }

  /* Prepare the template object that is the same for each game.  */
  UniValue tmpl(UniValue::VOBJ);
  if (pindex->nHeight > 0)
    {
      assert (pindex->pprev != nullptr);
      tmpl.pushKV ("parent", pindex->pprev->GetBlockHash ().GetHex ());
    }
  tmpl.pushKV ("child", block.GetHash ().GetHex ());
  if (!reqtoken.empty ())
    tmpl.pushKV ("reqtoken", reqtoken);
  tmpl.pushKV ("rngseed", block.GetRngSeed ().GetHex ());

  /* Send notifications for all games with the moves merged into the
     template object.  */
  for (const auto& game : games)
    {
      auto mit = perGameMoves.find (game);
      assert (mit != perGameMoves.end ());
      assert (mit->second.isArray ());

      UniValue data = tmpl;
      data.pushKV ("moves", mit->second);
      if (!SendMessage (commandPrefix + " json " + game, data))
        return false;
    }

  return true;
}

bool
ZMQGameBlocksNotifier::NotifyBlockAttached (const CBlock& block,
                                            const CBlockIndex* pindex)
{
  LOCK (csTrackedGames);
  return SendBlockNotifications (trackedGames, PREFIX_ATTACH, "",
                                 block, pindex);
}

bool
ZMQGameBlocksNotifier::NotifyBlockDetached (const CBlock& block,
                                            const CBlockIndex* pindex)
{
  LOCK (csTrackedGames);
  return SendBlockNotifications (trackedGames, PREFIX_DETACH, "",
                                 block, pindex);
}

UniValue
ZMQGameBlocksNotifier::GetTrackedGames () const
{
  LOCK (csTrackedGames);

  UniValue res(UniValue::VARR);
  for (const auto& g : trackedGames)
    res.push_back (g);

  return res;
}

void
ZMQGameBlocksNotifier::AddTrackedGame (const std::string& game)
{
  LOCK (csTrackedGames);
  trackedGames.insert (game);
}

void
ZMQGameBlocksNotifier::RemoveTrackedGame (const std::string& game)
{
  LOCK (csTrackedGames);
  trackedGames.erase (game);
}
