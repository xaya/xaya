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

  json_spirit::Object obj;
  obj.push_back (json_spirit::Pair ("name", nameStr));
  obj.push_back (json_spirit::Pair ("value", ValtypeToString (value)));

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

#ifdef ENABLE_WALLET

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
  std::string strError;
  strError = pwalletMain->SendMoneyToScript (newScript, LOCKED_AMOUNT, wtx);

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

#endif // ENABLE_WALLET?
