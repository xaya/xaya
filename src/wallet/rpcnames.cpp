// Copyright (c) 2014-2015 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "coins.h"
#include "init.h"
#include "main.h"
#include "names/common.h"
#include "names/main.h"
#include "primitives/transaction.h"
#include "random.h"
#include "rpcserver.h"
#include "script/names.h"
#include "txmempool.h"
#include "util.h"
#include "wallet/wallet.h"

#include <univalue.h>

/**
 * Helper routine to fetch the name output of a previous transaction.  This
 * is required for name_firstupdate.
 * @param txid Previous transaction ID.
 * @param txOut Set to the corresponding output.
 * @param txIn Set to the CTxIn to include in the new tx.
 * @return True if the output could be found.
 */
static bool
getNamePrevout (const uint256& txid, CTxOut& txOut, CTxIn& txIn)
{
  AssertLockHeld (cs_main);

  CCoins coins;
  if (!pcoinsTip->GetCoins (txid, coins))
    return false;

  for (unsigned i = 0; i < coins.vout.size (); ++i)
    if (!coins.vout[i].IsNull ()
        && CNameScript::isNameScript (coins.vout[i].scriptPubKey))
      {
        txOut = coins.vout[i];
        txIn = CTxIn (COutPoint (txid, i));
        return true;
      }

  return false;
}

/* ************************************************************************** */

UniValue
name_list (const UniValue& params, bool fHelp)
{
  if (!EnsureWalletIsAvailable (fHelp))
    return NullUniValue;

  if (fHelp || params.size () > 1)
    throw std::runtime_error (
        "name_list (\"name\")\n"
        "\nShow status of names in the wallet.\n"
        "\nArguments:\n"
        "1. \"name\"          (string, optional) only include this name\n"
        "\nResult:\n"
        "[\n"
        + getNameInfoHelp ("  ", ",") +
        "  ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_list", "")
        + HelpExampleCli ("name_list", "\"myname\"")
        + HelpExampleRpc ("name_list", "")
      );

  valtype nameFilter;
  if (params.size () == 1)
    nameFilter = ValtypeFromString (params[0].get_str ());

  std::map<valtype, int> mapHeights;
  std::map<valtype, UniValue> mapObjects;

  {
  LOCK2 (cs_main, pwalletMain->cs_wallet);
  BOOST_FOREACH (const PAIRTYPE(const uint256, CWalletTx)& item,
                 pwalletMain->mapWallet)
    {
      const CWalletTx& tx = item.second;
      if (!tx.IsNamecoin ())
        continue;

      CNameScript nameOp;
      int nOut = -1;
      for (unsigned i = 0; i < tx.vout.size (); ++i)
        {
          const CNameScript cur(tx.vout[i].scriptPubKey);
          if (cur.isNameOp ())
            {
              if (nOut != -1)
                LogPrintf ("ERROR: wallet contains tx with multiple"
                           " name outputs");
              else
                {
                  nameOp = cur;
                  nOut = i;
                }
            }
        }

      if (nOut == -1 || !nameOp.isAnyUpdate ())
        continue;

      const valtype& name = nameOp.getOpName ();
      if (!nameFilter.empty () && nameFilter != name)
        continue;

      const CBlockIndex* pindex;
      const int depth = tx.GetDepthInMainChain (pindex);
      if (depth <= 0)
        continue;

      const std::map<valtype, int>::const_iterator mit = mapHeights.find (name);
      if (mit != mapHeights.end () && mit->second > pindex->nHeight)
        continue;

      UniValue obj
        = getNameInfo (name, nameOp.getOpValue (),
                       COutPoint (tx.GetHash (), nOut),
                       nameOp.getAddress (), pindex->nHeight);

      const bool mine = IsMine (*pwalletMain, nameOp.getAddress ());
      obj.push_back (Pair ("transferred", !mine));

      mapHeights[name] = pindex->nHeight;
      mapObjects[name] = obj;
    }
  }

  UniValue res(UniValue::VARR);
  BOOST_FOREACH (const PAIRTYPE(const valtype, UniValue)& item, mapObjects)
    res.push_back (item.second);

  return res;
}

/* ************************************************************************** */

UniValue
name_new (const UniValue& params, bool fHelp)
{
  if (!EnsureWalletIsAvailable (fHelp))
    return NullUniValue;

  if (fHelp || params.size () != 1)
    throw std::runtime_error (
        "name_new \"name\"\n"
        "\nStart registration of the given name.  Must be followed up with"
        " name_firstupdate to finish the registration.\n"
        + HelpRequiringPassphrase () +
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to register\n"
        "\nResult:\n"
        "[\n"
        "  xxxxx,   (string) the txid, required for name_firstupdate\n"
        "  xxxxx    (string) random value for name_firstupdate\n"
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
  GetRandBytes (&rand[0], rand.size ());

  valtype toHash(rand);
  toHash.insert (toHash.end (), name.begin (), name.end ());
  const uint160 hash = Hash160 (toHash);

  /* No explicit locking should be necessary.  CReserveKey takes care
     of locking the wallet, and CommitTransaction (called when sending
     the tx) locks cs_main as necessary.  */

  EnsureWalletIsUnlocked ();

  CReserveKey keyName(pwalletMain);
  CPubKey pubKey;
  const bool ok = keyName.GetReservedKey (pubKey);
  assert (ok);
  const CScript addrName = GetScriptForDestination (pubKey.GetID ());
  const CScript newScript = CNameScript::buildNameNew (addrName, hash);

  CWalletTx wtx;
  SendMoneyToScript (newScript, NULL, NAME_LOCKED_AMOUNT, false, wtx);

  keyName.KeepKey ();

  const std::string randStr = HexStr (rand);
  const std::string txid = wtx.GetHash ().GetHex ();
  LogPrintf ("name_new: name=%s, rand=%s, tx=%s\n",
             nameStr.c_str (), randStr.c_str (), txid.c_str ());

  UniValue res(UniValue::VARR);
  res.push_back (txid);
  res.push_back (randStr);

  return res;
}

/* ************************************************************************** */

UniValue
name_firstupdate (const UniValue& params, bool fHelp)
{
  if (!EnsureWalletIsAvailable (fHelp))
    return NullUniValue;

  /* There is an undocumented sixth argument that can be used to disable
     the check for already existing names here (it will still be checked
     by the mempool and tx validation logic, of course).  This is used
     by the regtests to catch a bug that was previously present but
     has presumably no other use.  */

  if (fHelp || params.size () < 4 || params.size () > 6)
    throw std::runtime_error (
        "name_firstupdate \"name\" \"rand\" \"tx\" \"value\" (\"toaddress\")\n"
        "\nFinish the registration of a name.  Depends on name_new being"
        " already issued.\n"
        + HelpRequiringPassphrase () +
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

  {
    LOCK (mempool.cs);
    if (mempool.registersName (name))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "this name is already being registered");
  }

  if (params.size () < 6 || !params[5].get_bool ())
    {
      LOCK (cs_main);
      CNameData oldData;
      if (pcoinsTip->GetName (name, oldData) && !oldData.isExpired ())
        throw JSONRPCError (RPC_TRANSACTION_ERROR,
                            "this name is already active");
    }

  CTxOut prevOut;
  CTxIn txIn;
  {
    LOCK (cs_main);
    if (!getNamePrevout (prevTxid, prevOut, txIn))
      throw JSONRPCError (RPC_TRANSACTION_ERROR, "previous txid not found");
  }

  const CNameScript prevNameOp(prevOut.scriptPubKey);
  assert (prevNameOp.isNameOp ());
  if (prevNameOp.getNameOp () != OP_NAME_NEW)
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "previous tx is not name_new");

  valtype toHash(rand);
  toHash.insert (toHash.end (), name.begin (), name.end ());
  if (uint160 (prevNameOp.getOpHash ()) != Hash160 (toHash))
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "rand value is wrong");

  /* No more locking required, similarly to name_new.  */

  EnsureWalletIsUnlocked ();

  CReserveKey keyName(pwalletMain);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey (pubKeyReserve);
  assert (ok);
  bool usedKey = false;

  CScript addrName;
  if (params.size () >= 5)
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
  SendMoneyToScript (nameScript, &txIn, NAME_LOCKED_AMOUNT, false, wtx);

  if (usedKey)
    keyName.KeepKey ();

  return wtx.GetHash ().GetHex ();
}

/* ************************************************************************** */

UniValue
name_update (const UniValue& params, bool fHelp)
{
  if (!EnsureWalletIsAvailable (fHelp))
    return NullUniValue;

  if (fHelp || (params.size () != 2 && params.size () != 3))
    throw std::runtime_error (
        "name_update \"name\" \"value\" (\"toaddress\")\n"
        "\nUpdate a name and possibly transfer it.\n"
        + HelpRequiringPassphrase () +
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

  /* Reject updates to a name for which the mempool already has
     a pending update.  This is not a hard rule enforced by network
     rules, but it is necessary with the current mempool implementation.  */
  {
    LOCK (mempool.cs);
    if (mempool.updatesName (name))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "there is already a pending update for this name");
  }

  CNameData oldData;
  {
    LOCK (cs_main);
    if (!pcoinsTip->GetName (name, oldData) || oldData.isExpired ())
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "this name can not be updated");
  }

  const COutPoint outp = oldData.getUpdateOutpoint ();
  const CTxIn txIn(outp);

  /* No more locking required, similarly to name_new.  */

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
  SendMoneyToScript (nameScript, &txIn, NAME_LOCKED_AMOUNT, false, wtx);

  if (usedKey)
    keyName.KeepKey ();

  return wtx.GetHash ().GetHex ();
}

/* ************************************************************************** */

UniValue
sendtoname (const UniValue& params, bool fHelp)
{
  if (!EnsureWalletIsAvailable (fHelp))
    return NullUniValue;
  
  if (fHelp || params.size () < 2 || params.size () > 5)
    throw std::runtime_error (
        "sendtoname \"name\" amount ( \"comment\" \"comment-to\" subtractfeefromamount )\n"
        "\nSend an amount to the owner of a name. "
        " The amount is a real and is rounded to the nearest 0.00000001.\n"
        "\nIt is an error if the name is expired.\n"
        + HelpRequiringPassphrase () +
        "\nArguments:\n"
        "1. \"name\"        (string, required) The name to send to.\n"
        "2. \"amount\"      (numeric, required) The amount in nmc to send. eg 0.1\n"
        "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
        "                             This is not part of the transaction, just kept in your wallet.\n"
        "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
        "                             to which you're sending the transaction. This is not part of the \n"
        "                             transaction, just kept in your wallet.\n"
        "5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
        "                             The recipient will receive less namecoins than you enter in the amount field.\n"
        "\nResult:\n"
        "\"transactionid\"  (string) The transaction id.\n"
        "\nExamples:\n"
        + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1")
        + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"donation\" \"seans outpost\"")
        + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"\" \"\" true")
        + HelpExampleRpc ("sendtoname", "\"id/foobar\", 0.1, \"donation\", \"seans outpost\"")
      );

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  LOCK2 (cs_main, pwalletMain->cs_wallet);

  const std::string nameStr = params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);

  CNameData data;
  if (!pcoinsTip->GetName (name, data))
    {
      std::ostringstream msg;
      msg << "name not found: '" << nameStr << "'";
      throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, msg.str ());
    }
  if (data.isExpired ())
    throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "the name is expired");

  /* The code below is strongly based on sendtoaddress.  Make sure to
     keep it in sync.  */

  // Amount
  CAmount nAmount = AmountFromValue(params[1]);
  if (nAmount <= 0)
      throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

  // Wallet comments
  CWalletTx wtx;
  if (params.size() > 2 && !params[2].isNull() && !params[2].get_str().empty())
      wtx.mapValue["comment"] = params[2].get_str();
  if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
      wtx.mapValue["to"]      = params[3].get_str();

  bool fSubtractFeeFromAmount = false;
  if (params.size() > 4)
      fSubtractFeeFromAmount = params[4].get_bool();

  EnsureWalletIsUnlocked();

  SendMoneyToScript (data.getAddress (), NULL,
                     nAmount, fSubtractFeeFromAmount, wtx);

  return wtx.GetHash ().GetHex ();
}
