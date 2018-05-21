// Copyright (c) 2014-2018 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "coins.h"
#include "init.h"
#include "key_io.h"
#include "names/common.h"
#include "names/main.h"
#include "primitives/transaction.h"
#include "random.h"
#include "rpc/mining.h"
#include "rpc/server.h"
#include "script/names.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"
#include "wallet/coincontrol.h"
#include "wallet/wallet.h"

#include <univalue.h>

/* ************************************************************************** */

UniValue
name_list (const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;

  if (request.fHelp || request.params.size () > 1)
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

  RPCTypeCheck (request.params, {UniValue::VSTR});

  valtype nameFilter;
  if (request.params.size () == 1)
    nameFilter = ValtypeFromString (request.params[0].get_str ());

  std::map<valtype, int> mapHeights;
  std::map<valtype, UniValue> mapObjects;

  {
  LOCK2 (cs_main, pwallet->cs_wallet);
  for (const auto& item : pwallet->mapWallet)
    {
      const CWalletTx& tx = item.second;

      CNameScript nameOp;
      int nOut = -1;
      for (unsigned i = 0; i < tx.tx->vout.size (); ++i)
        {
          const CNameScript cur(tx.tx->vout[i].scriptPubKey);
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

      const bool mine = IsMine (*pwallet, nameOp.getAddress ());
      obj.pushKV ("transferred", !mine);

      mapHeights[name] = pindex->nHeight;
      mapObjects[name] = obj;
    }
  }

  UniValue res(UniValue::VARR);
  for (const auto& item : mapObjects)
    res.push_back (item.second);

  return res;
}

/* ************************************************************************** */

UniValue
name_register (const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;

  if (request.fHelp
      || (request.params.size () != 2 && request.params.size () != 3))
    throw std::runtime_error (
        "name_register \"name\" \"value\" (\"toaddress\")\n"
        "\nRegister a new name.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to register\n"
        "4. \"value\"         (string, required) value for the name\n"
        "5. \"toaddress\"     (string, optional) address to send the name to\n"
        "\nResult:\n"
        "\"txid\"             (string) the name_register's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_register", "\"myname\", \"new-value\"")
        + HelpExampleCli ("name_register", "\"myname\", \"new-value\", \"NEX4nME5p3iyNK3gFh4FUeUriHXxEFemo9\"")
        + HelpExampleRpc ("name_register", "\"myname\", \"new-value\"")
      );

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR});

  const std::string nameStr = request.params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);
  if (name.size () > MAX_NAME_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the name is too long");

  const std::string valueStr = request.params[1].get_str ();
  const valtype value = ValtypeFromString (valueStr);
  if (value.size () > MAX_VALUE_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the value is too long");

  /* Reject updates to a name for which the mempool already has
     a pending registration.  This is not a hard rule enforced by network
     rules, but it is necessary with the current mempool implementation.  */
  {
    LOCK (mempool.cs);
    if (mempool.registersName (name))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "there is already a pending registration"
                          " for this name");
  }

  {
    LOCK (cs_main);
    CNameData data;
    if (pcoinsTip->GetName (name, data))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "this name exists already");
  }

  /* No more locking required, similarly to name_new.  */

  EnsureWalletIsUnlocked (pwallet);

  CReserveKey keyName(pwallet);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey (pubKeyReserve, true);
  assert (ok);
  bool usedKey = false;

  CScript addrName;
  if (request.params.size () == 3)
    {
      keyName.ReturnKey ();
      const CTxDestination dest
        = DecodeDestination (request.params[2].get_str ());
      if (!IsValidDestination (dest))
        throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "invalid address");

      addrName = GetScriptForDestination (dest);
    }
  else
    {
      usedKey = true;
      addrName = GetScriptForDestination (pubKeyReserve.GetID ());
    }

  const CScript nameScript
    = CNameScript::buildNameRegister (addrName, name, value);

  CCoinControl coinControl;
  CTransactionRef tx = SendMoneyToScript (pwallet, nameScript, nullptr,
                                          NAME_LOCKED_AMOUNT, false,
                                          coinControl, {}, {});

  if (usedKey)
    keyName.KeepKey ();

  return tx->GetHash ().GetHex ();
}

/* ************************************************************************** */

UniValue
name_update (const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;

  if (request.fHelp
      || (request.params.size () != 2 && request.params.size () != 3))
    throw std::runtime_error (
        "name_update \"name\" \"value\" (\"toaddress\")\n"
        "\nUpdate a name and possibly transfer it.\n"
        + HelpRequiringPassphrase (pwallet) +
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

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR});

  const std::string nameStr = request.params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);
  if (name.size () > MAX_NAME_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the name is too long");

  const std::string valueStr = request.params[1].get_str ();
  const valtype value = ValtypeFromString (valueStr);
  if (value.size () > MAX_VALUE_LENGTH)
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
    if (!pcoinsTip->GetName (name, oldData))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "this name can not be updated");
  }

  const COutPoint outp = oldData.getUpdateOutpoint ();
  const CTxIn txIn(outp);

  /* No more locking required, similarly to name_new.  */

  EnsureWalletIsUnlocked (pwallet);

  CReserveKey keyName(pwallet);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey (pubKeyReserve, true);
  assert (ok);
  bool usedKey = false;

  CScript addrName;
  if (request.params.size () == 3)
    {
      keyName.ReturnKey ();
      const CTxDestination dest
        = DecodeDestination (request.params[2].get_str ());
      if (!IsValidDestination (dest))
        throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "invalid address");

      addrName = GetScriptForDestination (dest);
    }
  else
    {
      usedKey = true;
      addrName = GetScriptForDestination (pubKeyReserve.GetID ());
    }

  const CScript nameScript
    = CNameScript::buildNameUpdate (addrName, name, value);

  CCoinControl coinControl;
  CTransactionRef tx = SendMoneyToScript (pwallet, nameScript, &txIn,
                                          NAME_LOCKED_AMOUNT, false,
                                          coinControl, {}, {});

  if (usedKey)
    keyName.KeepKey ();

  return tx->GetHash ().GetHex ();
}

/* ************************************************************************** */

UniValue
sendtoname (const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;
  
  if (request.fHelp || request.params.size () < 2 || request.params.size () > 8)
    throw std::runtime_error (
        "sendtoname \"name\" amount ( \"comment\" \"comment_to\" subtractfeefromamount replaceable conf_target \"estimate_mode\")\n"
        "\nSend an amount to the owner of a name. "
        " The amount is a real and is rounded to the nearest 0.00000001.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"name\"        (string, required) The name to send to.\n"
        "2. \"amount\"      (numeric, required) The amount in CHI to send. eg 0.1\n"
        "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
        "                             This is not part of the transaction, just kept in your wallet.\n"
        "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
        "                             to which you're sending the transaction. This is not part of the \n"
        "                             transaction, just kept in your wallet.\n"
        "5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
        "                             The recipient will receive less CHI than you enter in the amount field.\n"
        "6. replaceable            (boolean, optional) Allow this transaction to be replaced by a transaction with higher fees via BIP 125\n"
        "7. conf_target            (numeric, optional) Confirmation target (in blocks)\n"
        "8. \"estimate_mode\"      (string, optional, default=UNSET) The fee estimate mode, must be one of:\n"
        "       \"UNSET\"\n"
        "       \"ECONOMICAL\"\n"
        "       \"CONSERVATIVE\"\n"
        "\nResult:\n"
        "\"transactionid\"  (string) The transaction id.\n"
        "\nExamples:\n"
        + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1")
        + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"donation\" \"seans outpost\"")
        + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"\" \"\" true")
        + HelpExampleRpc ("sendtoname", "\"id/foobar\", 0.1, \"donation\", \"seans outpost\"")
      );

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VNUM, UniValue::VSTR,
                 UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL,
                 UniValue::VNUM, UniValue::VSTR});

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Chimaera is downloading blocks...");

  LOCK2 (cs_main, pwallet->cs_wallet);

  const std::string nameStr = request.params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);

  CNameData data;
  if (!pcoinsTip->GetName (name, data))
    {
      std::ostringstream msg;
      msg << "name not found: '" << nameStr << "'";
      throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, msg.str ());
    }

  /* The code below is strongly based on sendtoaddress.  Make sure to
     keep it in sync.  */

  // Amount
  CAmount nAmount = AmountFromValue(request.params[1]);
  if (nAmount <= 0)
      throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

  // Wallet comments
  mapValue_t mapValue;
  if (request.params.size() > 2 && !request.params[2].isNull()
        && !request.params[2].get_str().empty())
      mapValue["comment"] = request.params[2].get_str();
  if (request.params.size() > 3 && !request.params[3].isNull()
        && !request.params[3].get_str().empty())
      mapValue["to"] = request.params[3].get_str();

  bool fSubtractFeeFromAmount = false;
  if (!request.params[4].isNull())
      fSubtractFeeFromAmount = request.params[4].get_bool();

  CCoinControl coin_control;
  if (!request.params[5].isNull()) {
      coin_control.m_signal_bip125_rbf = request.params[5].get_bool();
  }

  if (!request.params[6].isNull()) {
      coin_control.m_confirm_target = ParseConfirmTarget(request.params[6]);
  }

  if (!request.params[7].isNull()) {
      if (!FeeModeFromString(request.params[7].get_str(),
          coin_control.m_fee_mode)) {
          throw JSONRPCError(RPC_INVALID_PARAMETER,
                             "Invalid estimate_mode parameter");
      }
  }

  EnsureWalletIsUnlocked(pwallet);

  CTransactionRef tx = SendMoneyToScript (pwallet, data.getAddress (), nullptr,
                                          nAmount, fSubtractFeeFromAmount,
                                          coin_control, std::move(mapValue),
                                          {} /* fromAccount */);
  return tx->GetHash ().GetHex ();
}
