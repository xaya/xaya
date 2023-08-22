// Copyright (c) 2018-2023 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <zmq/zmqgames.h>

#include <chain.h>
#include <consensus/amount.h>
#include <core_io.h>
#include <key_io.h>
#include <logging.h>
#include <names/common.h>
#include <names/encoding.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/names.h>
#include <script/script.h>
#include <script/solver.h>
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
ZMQGameNotifier::SendZmqMessage (const std::string& command,
                                 const UniValue& data)
{
  const std::string dataStr = data.write ();
  return CZMQAbstractPublishNotifier::SendZmqMessage (
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

  /**
   * Type for the map that holds moves for each game.  Note that a single
   * transaction may contain multiple moves for a single game, namely if
   * it has duplicate JSON keys in the "g" object, or multiple "g" entries.
   * In those cases, we want to always store/send the last of them.
   */
  using MovePerGame = std::map<std::string, UniValue>;

  /** Move data for each game.  */
  MovePerGame moves;

  /** Set to true if this is an admin command.  */
  bool isAdmin = false;
  /** Game ID for which this is an admin command.  */
  std::string adminGame;
  /**
   * The array of admin command data (if any).  There can be multiple entries
   * if the move had duplicate "cmd" fields.
   */
  std::vector<UniValue> adminCmds;

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

  bool
  IsAdminCommand () const
  {
    return isAdmin;
  }

  const std::string&
  GetAdminGame () const
  {
    assert (isAdmin);
    return adminGame;
  }

  const std::vector<UniValue>&
  GetAdminCommands () const
  {
    assert (isAdmin);
    return adminCmds;
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
      isAdmin = true;
      adminGame = name.substr (2);
      assert (adminCmds.empty ());

      for (size_t i = 0; i < value.size (); ++i)
        if (value.getKeys ()[i] == "cmd")
          adminCmds.push_back (value.getValues ()[i]);

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
  tmpl.pushKV ("btxid", tx.GetBareHash ().GetHex ());
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
  std::map<valtype, CAmount> burns;
  for (const auto& out : tx.vout)
    {
      const CNameScript nameOp(out.scriptPubKey);
      if (nameOp.isNameOp ())
        continue;

      CTxDestination dest;
      if (ExtractDestination (out.scriptPubKey, dest))
        {
          const std::string addr = EncodeDestination (dest);
          outAmounts[addr] += out.nValue;
          continue;
        }

      valtype data;
      if (IsBurn (out.scriptPubKey, data))
        {
          burns[data] += out.nValue;
          continue;
        }
    }

  UniValue out(UniValue::VOBJ);
  for (const auto& entry : outAmounts)
    out.pushKV (entry.first, ValueFromAmount (entry.second));
  tmpl.pushKV ("out", out);

  /* Fill the per-game moves into the template.  */
  for (size_t i = 0; i < value.size (); ++i)
    {
      if (value.getKeys ()[i] != "g")
        continue;
      const auto& g = value.getValues ()[i];
      if (!g.isObject ())
        continue;

      for (size_t j = 0; j < g.size (); ++j)
        {
          UniValue obj = tmpl;
          obj.pushKV ("move", g.getValues ()[j]);

          const std::string& game = g.getKeys ()[j];

          const valtype burnData = ToByteVector ("g/" + game);
          const auto mitBurn = burns.find (burnData);
          if (mitBurn != burns.end ())
            obj.pushKV ("burnt", ValueFromAmount (mitBurn->second));
          else
            obj.pushKV ("burnt", 0);

          auto mit = moves.find (game);
          if (mit == moves.end ())
            moves.emplace (game, obj);
          else
            mit->second = obj;
        }
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

      if (data.IsAdminCommand ())
        {
          const auto& adminGame = data.GetAdminGame ();
          auto mit = perGameAdminCmds.find (adminGame);
          if (mit == perGameAdminCmds.end ())
            continue;

          assert (games.count (adminGame) > 0);
          assert (mit->second.isArray ());

          for (const auto& cmd : data.GetAdminCommands ())
            {
              UniValue cmdJson(UniValue::VOBJ);
              cmdJson.pushKV ("txid", tx->GetHash ().GetHex ());
              cmdJson.pushKV ("cmd", cmd);
              mit->second.push_back (cmdJson);
            }
        }
    }

  /* Prepare the template object that is the same for each game.  */
  UniValue blockData(UniValue::VOBJ);
  const uint256& blkHash = block.GetHash ();
  blockData.pushKV ("hash", blkHash.GetHex ());
  if (!block.hashPrevBlock.IsNull ())
    blockData.pushKV ("parent", block.hashPrevBlock.GetHex ());
  blockData.pushKV ("timestamp", block.GetBlockTime ());
  blockData.pushKV ("rngseed", block.GetRngSeed ().GetHex ());

  {
    LOCK (cs_main);
    const CBlockIndex* pindex = getIndexByHash (blkHash);
    assert (pindex != nullptr);
    blockData.pushKV ("height", pindex->nHeight);
    blockData.pushKV ("mediantime", pindex->GetMedianTimePast ());
  }

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

      if (!SendZmqMessage (commandPrefix + " json " + game, data))
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
ZMQGamePendingNotifier::NotifyTransactionAcceptance (const CTransaction& tx,
                                                     const uint64_t seq)
{
  const TransactionData data(tx);

  LOCK (trackedGames.cs);
  for (const auto& entry : data.GetMovesPerGame ())
    {
      if (trackedGames.games.count (entry.first) == 0)
        continue;

      std::ostringstream cmd;
      cmd << PREFIX_MOVE << " json " << entry.first;

      if (!SendZmqMessage (cmd.str (), entry.second))
        return false;
    }

  return true;
}
