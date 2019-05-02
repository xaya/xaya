// Copyright (c) 2018-2019 The Xaya developers
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
#include <validation.h>

#include <univalue.h>

#include <map>
#include <sstream>

const char* ZMQGameBlocksNotifier::PREFIX_ATTACH = "game-block-attach";
const char* ZMQGameBlocksNotifier::PREFIX_DETACH = "game-block-detach";

const char* ZMQGamePendingNotifier::PREFIX_MOVE = "game-pending-move";

UniValue
TrackedGames::Get () const
{
  LOCK (cs);

  UniValue res(UniValue::VARR);
  for (const auto& g : games)
    res.push_back (g);

  return res;
}

void
TrackedGames::Add (const std::string& game)
{
  LOCK (cs);
  games.insert (game);
}

void
TrackedGames::Remove (const std::string& game)
{
  LOCK (cs);
  games.erase (game);
}

bool
ZMQGameNotifier::SendMessage (const std::string& command,
                              const UniValue& data)
{
  const std::string dataStr = data.write ();
  return CZMQAbstractPublishNotifier::SendMessage (
      command.c_str (), dataStr.c_str (), dataStr.size ());
}

namespace
{

/**
 * Helper class that analyses a single transaction and extracts the data
 * from it that is relevant for the ZMQ game notifications.
 */
class TransactionData
{

private:

  /** Type for the map that holds moves for each game.  */
  using MovePerGame = std::map<std::string, UniValue>;

  /** Move data for each game.  */
  MovePerGame moves;

  /** Set to true if this is an admin command.  */
  bool isAdmin = false;
  /** Game ID for which this is an admin command.  */
  std::string adminGame;
  /** The admin command data (if any).  */
  UniValue adminCmd;

public:

  /**
   * Construct this by analysing a given transaction.
   */
  explicit TransactionData (const CTransaction& tx);

  TransactionData () = delete;
  TransactionData (const TransactionData&) = delete;
  void operator= (const TransactionData&) = delete;

  const MovePerGame&
  GetMovesPerGame () const
  {
    return moves;
  }

  /**
   * Checks if this is an admin command.  If it is, the game ID and
   * associated command value are returned.
   */
  bool
  IsAdminCommand (std::string& gameId, UniValue& cmd) const
  {
    if (!isAdmin)
      return false;

    gameId = adminGame;
    cmd = adminCmd;
    return true;
  }

};

TransactionData::TransactionData (const CTransaction& tx)
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
    return;

  /* Parse the value JSON.  */
  const std::string valueStr = EncodeName (nameOp.getOpValue (),
                                           NameEncoding::UTF8);
  UniValue value;
  if (!value.read (valueStr) || !value.isObject ())
    {
      /* This shouldn't actually happen, as the consensus rules check for
         these conditions for name updates.  But if it does happen, we just
         ignore it for here.  */
      LogPrintf ("%s: invalid value ignored\n", __func__);
      return;
    }

  /* Special case:  Handle admin commands.  */
  const std::string name = EncodeName (nameOp.getOpName (), NameEncoding::UTF8);
  if (name.substr (0, 2) == "g/")
    {
      if (!value.exists ("cmd"))
        return;

      isAdmin = true;
      adminGame = name.substr (2);
      adminCmd = value["cmd"];
      return;
    }
  assert (!isAdmin);

  /* Otherwise, we are only interested in p/ names.  */
  if (name.substr (0, 2) != "p/")
    return;

  /* See if there are actually games mentioned in the update's value.  */
  if (!value.exists ("g"))
    return;
  const UniValue& g = value["g"];
  if (!g.isObject () || g.empty ())
    return;

  /* Prepare a template object that is the same for all games.  */
  UniValue tmpl(UniValue::VOBJ);
  tmpl.pushKV ("txid", tx.GetHash ().GetHex ());
  tmpl.pushKV ("name", name.substr (2));

  UniValue inputs(UniValue::VARR);
  for (const auto& in : tx.vin)
    {
      UniValue cur(UniValue::VOBJ);
      cur.pushKV ("txid", in.prevout.hash.GetHex ());
      cur.pushKV ("vout", static_cast<int> (in.prevout.n));
      inputs.push_back (cur);
    }
  tmpl.pushKV ("inputs", inputs);

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
  for (const auto& game : g.getKeys ())
    {
      UniValue obj = tmpl;
      obj.pushKV ("move", g[game]);
      moves.emplace (game, obj);
    }
}

} // anonymous namespace

bool
ZMQGameBlocksNotifier::SendBlockNotifications (
    const std::set<std::string>& games, const std::string& commandPrefix,
    const std::string& reqtoken, const CBlock& block)
{
  /* Start with an empty array of moves and commands for each game.  */
  std::map<std::string, UniValue> perGameMoves;
  std::map<std::string, UniValue> perGameAdminCmds;
  for (const auto& game : games)
    {
      perGameMoves[game] = UniValue (UniValue::VARR);
      perGameAdminCmds[game] = UniValue (UniValue::VARR);
    }

  /* Add relevant moves and admin commands for each game from all the
     transactions to our arrays.  */
  for (const auto& tx : block.vtx)
    {
      const TransactionData data(*tx);

      for (const auto& entry : data.GetMovesPerGame ())
        {
          auto mit = perGameMoves.find (entry.first);
          if (mit == perGameMoves.end ())
            continue;

          assert (games.count (entry.first) > 0);
          assert (mit->second.isArray ());
          mit->second.push_back (entry.second);
        }

      std::string adminGame;
      UniValue adminCmd;
      if (data.IsAdminCommand (adminGame, adminCmd))
        {
          auto mit = perGameAdminCmds.find (adminGame);
          if (mit == perGameAdminCmds.end ())
            continue;

          assert (games.count (adminGame) > 0);
          assert (mit->second.isArray ());

          UniValue cmd(UniValue::VOBJ);
          cmd.pushKV ("txid", tx->GetHash ().GetHex ());
          cmd.pushKV ("cmd", adminCmd);

          mit->second.push_back (cmd);
        }
    }

  /* Prepare the template object that is the same for each game.  */
  UniValue blockData(UniValue::VOBJ);
  const uint256& blkHash = block.GetHash ();
  blockData.pushKV ("hash", blkHash.GetHex ());
  if (!block.hashPrevBlock.IsNull ())
    blockData.pushKV ("parent", block.hashPrevBlock.GetHex ());
  const CBlockIndex* pindex = LookupBlockIndex (blkHash);
  assert (pindex != nullptr);
  blockData.pushKV ("height", pindex->nHeight);
  blockData.pushKV ("timestamp", block.GetBlockTime ());
  blockData.pushKV ("rngseed", block.GetRngSeed ().GetHex ());

  UniValue tmpl(UniValue::VOBJ);
  tmpl.pushKV ("block", blockData);
  if (!reqtoken.empty ())
    tmpl.pushKV ("reqtoken", reqtoken);

  /* Send notifications for all games with the moves merged into the
     template object.  */
  for (const auto& game : games)
    {
      auto mitMv = perGameMoves.find (game);
      assert (mitMv != perGameMoves.end ());
      assert (mitMv->second.isArray ());

      auto mitCmd = perGameAdminCmds.find (game);
      assert (mitCmd != perGameAdminCmds.end ());
      assert (mitCmd->second.isArray ());

      UniValue data = tmpl;
      data.pushKV ("moves", mitMv->second);
      data.pushKV ("admin", mitCmd->second);

      if (!SendMessage (commandPrefix + " json " + game, data))
        return false;
    }

  return true;
}

bool
ZMQGameBlocksNotifier::NotifyBlockAttached (const CBlock& block)
{
  LOCK (trackedGames.cs);
  return SendBlockNotifications (trackedGames.games, PREFIX_ATTACH, "", block);
}

bool
ZMQGameBlocksNotifier::NotifyBlockDetached (const CBlock& block)
{
  LOCK (trackedGames.cs);
  return SendBlockNotifications (trackedGames.games, PREFIX_DETACH, "", block);
}

bool
ZMQGamePendingNotifier::NotifyPendingTx (const CTransaction& tx)
{
  const TransactionData data(tx);

  LOCK (trackedGames.cs);
  for (const auto& entry : data.GetMovesPerGame ())
    {
      if (trackedGames.games.count (entry.first) == 0)
        continue;

      std::ostringstream cmd;
      cmd << PREFIX_MOVE << " json " << entry.first;

      if (!SendMessage (cmd.str (), entry.second))
        return false;
    }

  return true;
}
