// Copyright (c) 2014-2018 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <coins.h>
#include <consensus/validation.h>
#include <init.h>
#include <key_io.h>
#include <names/common.h>
#include <names/main.h>
#include <net.h>
#include <primitives/transaction.h>
#include <random.h>
#include <rpc/mining.h>
#include <rpc/server.h>
#include <script/names.h>
#include <txmempool.h>
#include <util.h>
#include <utilmoneystr.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <univalue.h>

#include <algorithm>
#include <memory>

/* ************************************************************************** */
namespace
{

/**
 * A simple helper class that handles determination of the address to which
 * name outputs should be sent.  It handles the CReserveKey reservation
 * as well as parsing the explicit options given by the user (if any).
 */
class DestinationAddressHelper
{

private:

  /** Reference to the wallet that should be used.  */
  CWallet& wallet;

  /**
   * The reserve key that was used if no override is given.  When finalising
   * (after the sending succeeded), this key needs to be marked as KeepKey().
   */
  std::unique_ptr<CReserveKey> reserveKey;

  /** Set if a valid override destination was added.  */
  std::unique_ptr<CTxDestination> overrideDest;

public:

  explicit DestinationAddressHelper (CWallet& w)
    : wallet(w)
  {}

  /**
   * Processes the given options object to see if it contains an override
   * destination.  If it does, remembers it.
   */
  void setOptions (const UniValue& opt);

  /**
   * Returns the script that should be used as destination.
   */
  CScript getScript ();

  /**
   * Marks the key as used if one has been reserved.  This should be called
   * when sending succeeded.
   */
  void finalise ();

};

void DestinationAddressHelper::setOptions (const UniValue& opt)
{
  RPCTypeCheckObj (opt,
    {
      {"destAddress", UniValueType (UniValue::VSTR)},
    },
    true, false);
  if (!opt.exists ("destAddress"))
    return;

  CTxDestination dest = DecodeDestination (opt["destAddress"].get_str ());
  if (!IsValidDestination (dest))
    throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "invalid address");
  overrideDest.reset (new CTxDestination (std::move (dest)));
}

CScript DestinationAddressHelper::getScript ()
{
  if (overrideDest != nullptr)
    return GetScriptForDestination (*overrideDest);

  reserveKey.reset (new CReserveKey (&wallet));
  CPubKey pubKeyReserve;
  const bool ok = reserveKey->GetReservedKey (pubKeyReserve, true);
  assert (ok);

  return GetScriptForDestination (pubKeyReserve.GetID ());
}

void DestinationAddressHelper::finalise ()
{
  if (reserveKey != nullptr)
    reserveKey->KeepKey ();
}

/**
 * Returns the help text for the options argument for name operations.
 */
std::string getNameOpOptionsHelp ()
{
  return "  {\n"
         "    \"destAddress\"  (string, optional) The address to send the name output to\n"
         "    \"sendCoins\"    (object, optional) Addresses to which coins should be sent additionally\n"
         "      {\n"
         "        \"addr1\": x,\n"
         "        \"addr2\": y,\n"
         "        ...\n"
         "      }\n"
         "  }\n";
}

/**
 * Sends a name output to the given name script.  This is the "final" step that
 * is common between name_new, name_firstupdate and name_update.  This method
 * also implements the "sendCoins" option, if included.
 */
CTransactionRef
SendNameOutput (CWallet& wallet, const CScript& nameOutScript,
                const CTxIn* nameInput, const UniValue& opt)
{
  RPCTypeCheckObj (opt,
    {
      {"sendCoins", UniValueType (UniValue::VOBJ)},
    },
    true, false);

  if (wallet.GetBroadcastTransactions () && !g_connman)
    throw JSONRPCError (RPC_CLIENT_P2P_DISABLED,
                        "Error: Peer-to-peer functionality missing"
                        " or disabled");

  std::vector<CRecipient> vecSend;
  vecSend.push_back ({nameOutScript, NAME_LOCKED_AMOUNT, false});

  if (opt.exists ("sendCoins"))
    for (const std::string& addr : opt["sendCoins"].getKeys ())
      {
        const CTxDestination dest = DecodeDestination (addr);
        if (!IsValidDestination (dest))
          throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY,
                              "Invalid address: " + addr);

        const CAmount nAmount = AmountFromValue (opt["sendCoins"][addr]);
        if (nAmount <= 0)
          throw JSONRPCError (RPC_TYPE_ERROR, "Invalid amount for send");

        vecSend.push_back ({GetScriptForDestination (dest), nAmount, false});
      }

  /* Shuffle the recipient list for privacy.  */
  std::shuffle (vecSend.begin (), vecSend.end (), FastRandomContext ());

  /* Check balance against total amount sent.  If we have a name input, we have
     to take its value into account.  */

  const CAmount curBalance = wallet.GetBalance ();

  CAmount totalSpend = 0;
  for (const auto& recv : vecSend)
    totalSpend += recv.nAmount;

  CAmount lockedValue = 0;
  std::string strError;
  if (nameInput != nullptr)
    {
      const CWalletTx* dummyWalletTx;
      if (!wallet.FindValueInNameInput (*nameInput, lockedValue,
                                        dummyWalletTx, strError))
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

  if (totalSpend > curBalance + lockedValue)
    throw JSONRPCError (RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

  /* Create and send the transaction.  This code is based on the corresponding
     part of SendMoneyToScript and should stay in sync.  */

  CCoinControl coinControl;
  CReserveKey keyChange(&wallet);
  CAmount nFeeRequired;
  int nChangePosRet = -1;

  CTransactionRef tx;
  if (!wallet.CreateTransaction (vecSend, nameInput, tx, keyChange,
                                 nFeeRequired, nChangePosRet, strError,
                                 coinControl))
    {
      if (totalSpend + nFeeRequired > curBalance)
        strError = strprintf ("Error: This transaction requires a transaction"
                              " fee of at least %s",
                              FormatMoney (nFeeRequired));
      throw JSONRPCError (RPC_WALLET_ERROR, strError);
    }

  CValidationState state;
  if (!wallet.CommitTransaction (tx, {}, {}, {}, keyChange, g_connman.get (),
                                 state))
    {
      strError = strprintf ("Error: The transaction was rejected!"
                            "  Reason given: %s", FormatStateMessage (state));
      throw JSONRPCError (RPC_WALLET_ERROR, strError);
    }

  return tx;
}

} // anonymous namespace
/* ************************************************************************** */

UniValue
name_list (const JSONRPCRequest& request)
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  CWallet* const pwallet = wallet.get ();

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
        + NameInfoHelp ("  ")
            .finish (",") +
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

      const auto mit = mapHeights.find (name);
      if (mit != mapHeights.end () && mit->second > pindex->nHeight)
        continue;

      UniValue obj
        = getNameInfo (name, nameOp.getOpValue (),
                       COutPoint (tx.GetHash (), nOut),
                       nameOp.getAddress ());
      addOwnershipInfo (nameOp.getAddress (), pwallet, obj);

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
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  CWallet* const pwallet = wallet.get ();

  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;

  if (request.fHelp
      || (request.params.size () != 2 && request.params.size () != 3))
    throw std::runtime_error (
        "name_register \"name\" \"value\" (\"options\")\n"
        "\nRegister a new name.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to register\n"
        "2. \"value\"         (string, required) value for the name\n"
        "3. \"options\"       (object, optional)\n"
        + getNameOpOptionsHelp () +
        "\nResult:\n"
        "\"txid\"             (string) the name_register's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_register", "\"myname\", \"new-value\"")
        + HelpExampleCli ("name_register", "\"myname\", \"new-value\", \"NEX4nME5p3iyNK3gFh4FUeUriHXxEFemo9\"")
        + HelpExampleRpc ("name_register", "\"myname\", \"new-value\"")
      );

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VSTR, UniValue::VOBJ});

  const std::string nameStr = request.params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);

  CValidationState state;
  if (!IsNameValid (name, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  const std::string valueStr = request.params[1].get_str ();
  const valtype value = ValtypeFromString (valueStr);
  if (!IsValueValid (value, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 3)
    options = request.params[2].get_obj ();

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

  DestinationAddressHelper destHelper(*pwallet);
  destHelper.setOptions (options);

  const CScript nameScript
    = CNameScript::buildNameRegister (destHelper.getScript (), name, value);

  CCoinControl coinControl;
  CTransactionRef tx = SendNameOutput (*pwallet, nameScript, nullptr, options);
  destHelper.finalise ();

  return tx->GetHash ().GetHex ();
}

/* ************************************************************************** */

UniValue
name_update (const JSONRPCRequest& request)
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  CWallet* const pwallet = wallet.get ();

  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;

  if (request.fHelp || request.params.size () < 2 || request.params.size () > 3)
    throw std::runtime_error (
        "name_update \"name\" \"value\" (\"options\")\n"
        "\nUpdate a name and possibly transfer it.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to update\n"
        "2. \"value\"         (string, required) value for the name\n"
        "3. \"options\"       (object, optional)\n"
        + getNameOpOptionsHelp () +
        "\nResult:\n"
        "\"txid\"             (string) the name_update's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_update", "\"myname\", \"new-value\"")
        + HelpExampleRpc ("name_update", "\"myname\", \"new-value\"")
      );

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VSTR, UniValue::VOBJ});

  const std::string nameStr = request.params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);

  CValidationState state;
  if (!IsNameValid (name, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  const std::string valueStr = request.params[1].get_str ();
  const valtype value = ValtypeFromString (valueStr);
  if (!IsValueValid (value, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 3)
    options = request.params[2].get_obj ();

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

  DestinationAddressHelper destHelper(*pwallet);
  destHelper.setOptions (options);

  const CScript nameScript
    = CNameScript::buildNameUpdate (destHelper.getScript (), name, value);

  CCoinControl coinControl;
  CTransactionRef tx = SendNameOutput (*pwallet, nameScript, &txIn, options);
  destHelper.finalise ();

  return tx->GetHash ().GetHex ();
}

/* ************************************************************************** */

UniValue
sendtoname (const JSONRPCRequest& request)
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  CWallet* const pwallet = wallet.get ();

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
                       "Xyon is downloading blocks...");

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
