// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chainparams.h"
#include "core.h"
#include "init.h"
#include "main.h"
#include "names.h"
#include "random.h"
#include "rpcserver.h"
#include "util.h"

#include "script/names.h"

#ifdef ENABLE_WALLET
# include "wallet.h"
#endif

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include <sstream>

/**
 * Amount to lock in name transactions.  This is not (yet) enforced by the
 * protocol.  Thus just a constant here for now.
 */
static const CAmount LOCKED_AMOUNT = COIN / 100; 

/* ************************************************************************** */

json_spirit::Value
name_show (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || params.size () != 1)
    throw std::runtime_error (
        "name_show \"name\"\n"
        "\nLook up the current data for the given name."
        "  Fails if the name doesn't exist.\n"
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to query for\n"
        "\nResult:\n"
        "{\n"
        "  \"name\": xxxxx,           (string) the requested name\n"
        "  \"value\": xxxxx,          (string) the name's current value\n"
        "  \"txid\": xxxxx,           (string) the name's last update tx\n"
        "  \"address\": xxxxx,        (string) the address holding the name\n"
        "  \"height\": xxxxx,         (numeric) the name's last update height\n"
        "  \"expires_in\": xxxxx,     (numeric) expire counter for the name\n"
        "  \"expired\": xxxxx,        (boolean) whether the name is expired\n"
        "}\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_show", "\"myname\"")
        + HelpExampleRpc ("name_show", "\"myname\"")
      );

  const std::string nameStr = params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);

  CNameData data;
  if (!pcoinsTip->GetName (name, data))
    {
      std::ostringstream msg;
      msg << "name not found: '" << nameStr << "'";
      throw JSONRPCError (RPC_WALLET_ERROR, msg.str ());
    }

  const valtype& value = data.getValue ();
  const uint256& txid = data.getUpdateTx ();

  json_spirit::Object obj;
  obj.push_back (json_spirit::Pair ("name", nameStr));
  obj.push_back (json_spirit::Pair ("value", ValtypeToString (value)));
  obj.push_back (json_spirit::Pair ("txid", txid.GetHex ()));

  /* Try to extract the address.  May fail if we can't parse the script
     as a "standard" script.  */
  CTxDestination dest;
  CBitcoinAddress addr;
  std::string addrStr;
  if (ExtractDestination (data.getAddress (), dest) && addr.Set (dest))
    addrStr = addr.ToString ();
  else
    addrStr = "<nonstandard>";
  obj.push_back (json_spirit::Pair ("address", addrStr));

  /* Calculate expiration data.  */
  const int nameHeight = data.getHeight ();
  const int curHeight = chainActive.Height ();
  const int expireDepth = Params ().NameExpirationDepth (curHeight);
  const int expireHeight = nameHeight + expireDepth;
  const int expiresIn = expireHeight - curHeight;
  const bool expired = (expiresIn <= 0);
  obj.push_back (json_spirit::Pair ("height", nameHeight));
  obj.push_back (json_spirit::Pair ("expires_in", expiresIn));
  obj.push_back (json_spirit::Pair ("expired", expired));

  return obj;
}

/* ************************************************************************** */

#ifdef ENABLE_WALLET

/**
 * Helper routine to fetch the name output of a previous transaction.  This
 * is required for name_firstupdate and name_update.
 * @param txid Previous transaction ID.
 * @param txOut Set to the corresponding output.
 * @param txIn Set to the CTxIn to include in the new tx.
 * @return True if the output could be found.
 */
static bool
getNamePrevout (const uint256& txid, CTxOut& txOut, CTxIn& txIn)
{
  CCoins coins;
  if (!pcoinsTip->GetCoins (txid, coins))
    return false;

  for (unsigned i = 0; i < coins.vout.size (); ++i)
    if (!coins.vout[i].IsNull ())
      if (CNameScript::isNameScript (coins.vout[i].scriptPubKey))
        {
          txOut = coins.vout[i];
          txIn = CTxIn (COutPoint (txid, i));
          return true;
        }

  return false;
}

/* ************************************************************************** */

json_spirit::Value
name_new (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || params.size () != 1)
    throw std::runtime_error (
        "name_new \"name\"\n"
        "\nStart registration of the given name.  Must be followed up with"
        " name_firstupdate to finish the registration.\n"
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to register\n"
        "\nResult:\n"
        "[\n"
        "  \"txid\": xxxxx,   (string) the txid, required for name_firstupdate\n"
        "  \"rand\": xxxxx,   (string) random value for name_firstupdate\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_new", "\"myname\"")
        + HelpExampleRpc ("name_new", "\"myname\"")
      );

  const std::string nameStr = params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);
  if (name.size () > MAX_NAME_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the name is too long");

  valtype rand(20);
  if (!GetRandBytes (&rand[0], rand.size ()))
    throw std::runtime_error ("failed to generate random value");

  valtype toHash(rand);
  toHash.insert (toHash.end (), name.begin (), name.end ());
  const uint160 hash = Hash160 (toHash);

  EnsureWalletIsUnlocked ();

  CReserveKey keyName(pwalletMain);
  CPubKey pubKey;
  const bool ok = keyName.GetReservedKey (pubKey);
  assert (ok);
  const CScript addrName = GetScriptForDestination (pubKey.GetID ());
  const CScript newScript = CNameScript::buildNameNew (addrName, hash);

  CWalletTx wtx;
  const std::string strError
    = pwalletMain->SendMoneyToScript (newScript, NULL, LOCKED_AMOUNT, wtx);

  if (strError != "")
    {
      keyName.ReturnKey ();
      throw JSONRPCError (RPC_WALLET_ERROR, strError);
    }

  keyName.KeepKey ();

  const std::string randStr = HexStr (rand);
  const std::string txid = wtx.GetHash ().GetHex ();
  LogPrintf ("name_new: name=%s, rand=%s, tx=%s\n",
             nameStr.c_str (), randStr.c_str (), txid.c_str ());

  json_spirit::Array res;
  res.push_back (txid);
  res.push_back (randStr);

  return res;
}

/* ************************************************************************** */

json_spirit::Value
name_firstupdate (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || (params.size () != 4 && params.size () != 5))
    throw std::runtime_error (
        "name_firstupdate \"name\" \"rand\" \"tx\" \"value\" [\"toaddress\"]\n"
        "\nFinish the registration of a name.  Depends on name_new being"
        " already issued.\n"
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to register\n"
        "2. \"rand\"          (string, required) the rand value of name_new\n"
        "3. \"tx\"            (string, required) the name_new txid\n"
        "4. \"value\"         (string, required) value for the name\n"
        "5. \"toaddress\"     (string, optional) address to send the name to\n"
        "\nResult:\n"
        "\"txid\"             (string) the name_firstupdate's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_firstupdate", "\"myname\", \"555844f2db9c7f4b25da6cb8277596de45021ef2\" \"a77ceb22aa03304b7de64ec43328974aeaca211c37dd29dcce4ae461bb80ca84\", \"my-value\"")
        + HelpExampleCli ("name_firstupdate", "\"myname\", \"555844f2db9c7f4b25da6cb8277596de45021ef2\" \"a77ceb22aa03304b7de64ec43328974aeaca211c37dd29dcce4ae461bb80ca84\", \"my-value\", \"NEX4nME5p3iyNK3gFh4FUeUriHXxEFemo9\"")
        + HelpExampleRpc ("name_firstupdate", "\"myname\", \"555844f2db9c7f4b25da6cb8277596de45021ef2\" \"a77ceb22aa03304b7de64ec43328974aeaca211c37dd29dcce4ae461bb80ca84\", \"my-value\"")
      );

  const std::string nameStr = params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);
  if (name.size () > MAX_NAME_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the name is too long");

  const valtype rand = ParseHexV (params[1], "rand");
  if (rand.size () > 20)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid rand value");

  const uint256 prevTxid = ParseHashV (params[2], "txid");

  const std::string valueStr = params[3].get_str ();
  const valtype value = ValtypeFromString (valueStr);
  if (value.size () > MAX_VALUE_LENGTH_UI)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the value is too long");

  CNameData oldData;
  if (pcoinsTip->GetName (name, oldData) && !oldData.isExpired ())
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "this name is already active");

  CTxOut prevOut;
  CTxIn txIn;
  if (!getNamePrevout (prevTxid, prevOut, txIn))
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "previous txid not found");

  const CNameScript prevNameOp(prevOut.scriptPubKey);
  assert (prevNameOp.isNameOp ());
  if (prevNameOp.getNameOp () != OP_NAME_NEW)
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "previous tx is not name_new");

  valtype toHash(rand);
  toHash.insert (toHash.end (), name.begin (), name.end ());
  if (uint160 (prevNameOp.getOpHash ()) != Hash160 (toHash))
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "rand value is wrong");

  EnsureWalletIsUnlocked ();

  CReserveKey keyName(pwalletMain);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey (pubKeyReserve);
  assert (ok);
  bool usedKey = false;

  CScript addrName;
  if (params.size () == 5)
    {
      keyName.ReturnKey ();
      const CBitcoinAddress toAddress(params[4].get_str ());
      if (!toAddress.IsValid ())
        throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "invalid address");

      addrName = GetScriptForDestination (toAddress.Get ());
    }
  else
    {
      usedKey = true;
      addrName = GetScriptForDestination (pubKeyReserve.GetID ());
    }

  const CScript nameScript
    = CNameScript::buildNameFirstupdate (addrName, name, value, rand);

  CWalletTx wtx;
  const std::string strError
    = pwalletMain->SendMoneyToScript (nameScript, &txIn, LOCKED_AMOUNT, wtx);

  if (strError != "")
    {
      if (usedKey)
        keyName.ReturnKey ();
      throw JSONRPCError (RPC_WALLET_ERROR, strError);
    }

  if (usedKey)
    keyName.KeepKey ();

  return wtx.GetHash ().GetHex ();
}

/* ************************************************************************** */

json_spirit::Value
name_update (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || (params.size () != 2 && params.size () != 3))
    throw std::runtime_error (
        "name_update \"name\" \"value\" [\"toaddress\"]\n"
        "\nUpdate a name and possibly transfer it.\n"
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to update\n"
        "4. \"value\"         (string, required) value for the name\n"
        "5. \"toaddress\"     (string, optional) address to send the name to\n"
        "\nResult:\n"
        "\"txid\"             (string) the name_update's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_update", "\"myname\", \"new-value\"")
        + HelpExampleCli ("name_update", "\"myname\", \"new-value\", \"NEX4nME5p3iyNK3gFh4FUeUriHXxEFemo9\"")
        + HelpExampleRpc ("name_update", "\"myname\", \"new-value\"")
      );

  const std::string nameStr = params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);
  if (name.size () > MAX_NAME_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the name is too long");

  const std::string valueStr = params[1].get_str ();
  const valtype value = ValtypeFromString (valueStr);
  if (value.size () > MAX_VALUE_LENGTH_UI)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the value is too long");

  CNameData oldData;
  if (!pcoinsTip->GetName (name, oldData) || oldData.isExpired ())
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "this name can not be updated");

  CTxOut prevOut;
  CTxIn txIn;
  if (!getNamePrevout (oldData.getUpdateTx (), prevOut, txIn))
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "previous txid not found");

  EnsureWalletIsUnlocked ();

  CReserveKey keyName(pwalletMain);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey (pubKeyReserve);
  assert (ok);
  bool usedKey = false;

  CScript addrName;
  if (params.size () == 3)
    {
      keyName.ReturnKey ();
      const CBitcoinAddress toAddress(params[2].get_str ());
      if (!toAddress.IsValid ())
        throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "invalid address");

      addrName = GetScriptForDestination (toAddress.Get ());
    }
  else
    {
      usedKey = true;
      addrName = GetScriptForDestination (pubKeyReserve.GetID ());
    }

  const CScript nameScript
    = CNameScript::buildNameUpdate (addrName, name, value);

  CWalletTx wtx;
  const std::string strError
    = pwalletMain->SendMoneyToScript (nameScript, &txIn, LOCKED_AMOUNT, wtx);

  if (strError != "")
    {
      if (usedKey)
        keyName.ReturnKey ();
      throw JSONRPCError (RPC_WALLET_ERROR, strError);
    }

  if (usedKey)
    keyName.KeepKey ();

  return wtx.GetHash ().GetHex ();
}

#endif // ENABLE_WALLET?
