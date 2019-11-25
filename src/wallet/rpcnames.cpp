// Copyright (c) 2014-2019 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <coins.h>
#include <consensus/validation.h>
#include <init.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <names/common.h>
#include <names/encoding.h>
#include <names/main.h>
#include <names/mempool.h>
#include <node/context.h>
#include <net.h>
#include <primitives/transaction.h>
#include <random.h>
#include <rpc/blockchain.h>
#include <rpc/names.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/names.h>
#include <txmempool.h>
#include <util/fees.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/validation.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
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
   * (after the sending succeeded), this key needs to be marked as Keep().
   */
  std::unique_ptr<ReserveDestination> rdest;

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

  rdest.reset (new ReserveDestination (&wallet, wallet.m_default_address_type));
  CTxDestination dest;
  if (!rdest->GetReservedDestination (dest, false))
    throw JSONRPCError (RPC_WALLET_KEYPOOL_RAN_OUT,
                        "Error: Keypool ran out,"
                        " please call keypoolrefill first");

  return GetScriptForDestination (dest);
}

void DestinationAddressHelper::finalise ()
{
  if (rdest != nullptr)
    rdest->KeepDestination ();
}

/**
 * Sends a name output to the given name script.  This is the "final" step that
 * is common between name_new, name_firstupdate and name_update.  This method
 * also implements the "sendCoins" option, if included.
 */
CTransactionRef
SendNameOutput (interfaces::Chain::Lock& locked_chain,
                CWallet& wallet, const CScript& nameOutScript,
                const CTxIn* nameInput, const UniValue& opt)
{
  RPCTypeCheckObj (opt,
    {
      {"sendCoins", UniValueType (UniValue::VOBJ)},
    },
    true, false);

  if (wallet.GetBroadcastTransactions () && !g_rpc_node->connman)
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

  const CAmount curBalance = wallet.GetBalance ().m_mine_trusted;

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
  CAmount nFeeRequired;
  int nChangePosRet = -1;

  CTransactionRef tx;
  if (!wallet.CreateTransaction (locked_chain, vecSend, nameInput, tx,
                                 nFeeRequired, nChangePosRet, strError,
                                 coinControl))
    {
      if (totalSpend + nFeeRequired > curBalance)
        strError = strprintf ("Error: This transaction requires a transaction"
                              " fee of at least %s",
                              FormatMoney (nFeeRequired));
      throw JSONRPCError (RPC_WALLET_ERROR, strError);
    }

  wallet.CommitTransaction (tx, {}, {});
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

  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ();

  RPCHelpMan ("name_list",
      "\nShows the status of all names in the wallet.\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Only include this name"},
          optHelp.buildRpcArg (),
      },
      RPCResult {
        "[\n"
        + NameInfoHelp ("  ")
            .withHeight ()
            .finish (",") +
        "  ...\n"
        "]\n"
      },
      RPCExamples {
          HelpExampleCli ("name_list", "")
        + HelpExampleCli ("name_list", "\"myname\"")
        + HelpExampleRpc ("name_list", "")
      }
  ).Check (request);

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VOBJ}, true);

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 2)
    options = request.params[1].get_obj ();

  valtype nameFilter;
  if (request.params.size () >= 1 && !request.params[0].isNull ())
    nameFilter = DecodeNameFromRPCOrThrow (request.params[0], options);

  std::map<valtype, int> mapHeights;
  std::map<valtype, UniValue> mapObjects;

  /* Make sure the results are valid at least up to the most recent block
     the user could have gotten from another RPC command prior to now.  */
  pwallet->BlockUntilSyncedToCurrentChain ();

  {
  LOCK (pwallet->cs_wallet);

  const int tipHeight = ::ChainActive ().Height ();
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

      const int depth = tx.GetDepthInMainChain ();
      if (depth <= 0)
        continue;
      const int height = tipHeight - depth + 1;

      const auto mit = mapHeights.find (name);
      if (mit != mapHeights.end () && mit->second > height)
        continue;

      UniValue obj
        = getNameInfo (options, name, nameOp.getOpValue (),
                       COutPoint (tx.GetHash (), nOut),
                       nameOp.getAddress ());
      addOwnershipInfo (nameOp.getAddress (), pwallet, obj);
      addHeightInfo (height, obj);

      mapHeights[name] = height;
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

  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ()
      .withWriteOptions ();

  RPCHelpMan ("name_register",
      "\nRegisters a new name."
          + HelpRequiringPassphrase (pwallet) + "\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to register"},
          {"value", RPCArg::Type::STR, RPCArg::Optional::NO, "Value for the name"},
          optHelp.buildRpcArg (),
      },
      RPCResult {
        "\"txid\"             (string) the name_register's txid\n"
      },
      RPCExamples {
          HelpExampleCli ("name_register", "\"myname\", \"new-value\"")
        + HelpExampleRpc ("name_register", "\"myname\", \"new-value\"")
      }
  ).Check (request);

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VSTR, UniValue::VOBJ});

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 3)
    options = request.params[2].get_obj ();

  const valtype name = DecodeNameFromRPCOrThrow (request.params[0], options);
  TxValidationState state;
  if (!IsNameValid (name, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  const valtype value = DecodeValueFromRPCOrThrow (request.params[1], options);
  if (!IsValueValid (value, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

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
    if (::ChainstateActive ().CoinsTip ().GetName (name, data))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "this name exists already");
  }

  /* Make sure the results are valid at least up to the most recent block
     the user could have gotten from another RPC command prior to now.  */
  pwallet->BlockUntilSyncedToCurrentChain ();

  auto locked_chain = pwallet->chain ().lock ();
  LOCK (pwallet->cs_wallet);

  EnsureWalletIsUnlocked (pwallet);

  DestinationAddressHelper destHelper(*pwallet);
  destHelper.setOptions (options);

  const CScript nameScript
    = CNameScript::buildNameRegister (destHelper.getScript (), name, value);

  CTransactionRef tx = SendNameOutput (*locked_chain, *pwallet,
                                       nameScript, nullptr, options);
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

  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ()
      .withWriteOptions ();

  RPCHelpMan ("name_update",
      "\nUpdates a name and possibly transfers it."
          + HelpRequiringPassphrase (pwallet) + "\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to update"},
          {"value", RPCArg::Type::STR, RPCArg::Optional::NO, "Value for the name"},
          optHelp.buildRpcArg (),
      },
      RPCResult {
        "\"txid\"             (string) the name_update's txid\n"
      },
      RPCExamples {
          HelpExampleCli ("name_update", "\"myname\", \"new-value\"")
        + HelpExampleRpc ("name_update", "\"myname\", \"new-value\"")
      }
  ).Check (request);

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VSTR, UniValue::VOBJ});

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 3)
    options = request.params[2].get_obj ();

  const valtype name = DecodeNameFromRPCOrThrow (request.params[0], options);
  TxValidationState state;
  if (!IsNameValid (name, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  const valtype value = DecodeValueFromRPCOrThrow (request.params[1], options);
  if (!IsValueValid (value, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  /* For finding the name output to spend, we first check if there are
     pending operations on the name in the mempool.  If there are, then we
     build upon the last one to get a valid chain.  If there are none, then we
     look up the last outpoint from the name database instead.  */

  const unsigned chainLimit = gArgs.GetArg ("-limitnamechains",
                                            DEFAULT_NAME_CHAIN_LIMIT);
  COutPoint outp;
  {
    LOCK (mempool.cs);

    const unsigned pendingOps = mempool.pendingNameChainLength (name);
    if (pendingOps >= chainLimit)
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "there are already too many pending operations"
                          " on this name");

    if (pendingOps > 0)
      outp = mempool.lastNameOutput (name);
  }

  if (outp.IsNull ())
    {
      LOCK (cs_main);

      CNameData oldData;
      const auto& coinsTip = ::ChainstateActive ().CoinsTip ();
      if (!coinsTip.GetName (name, oldData))
        throw JSONRPCError (RPC_TRANSACTION_ERROR,
                            "this name can not be updated");

      outp = oldData.getUpdateOutpoint ();
    }

  assert (!outp.IsNull ());
  const CTxIn txIn(outp);

  /* Make sure the results are valid at least up to the most recent block
     the user could have gotten from another RPC command prior to now.  */
  pwallet->BlockUntilSyncedToCurrentChain ();

  auto locked_chain = pwallet->chain ().lock ();
  LOCK (pwallet->cs_wallet);

  EnsureWalletIsUnlocked (pwallet);

  DestinationAddressHelper destHelper(*pwallet);
  destHelper.setOptions (options);

  const CScript nameScript
    = CNameScript::buildNameUpdate (destHelper.getScript (), name, value);

  CTransactionRef tx = SendNameOutput (*locked_chain, *pwallet,
                                       nameScript, &txIn, options);
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
  
  RPCHelpMan{"sendtoname",
      "\nSend an amount to the owner of a name.\n"
      "\nIt is an error if the name is expired."
          + HelpRequiringPassphrase(pwallet) + "\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to send to."},
          {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount in " + CURRENCY_UNIT + " to send. eg 0.1"},
          {"comment", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A comment used to store what the transaction is for.\n"
  "                             This is not part of the transaction, just kept in your wallet."},
          {"comment_to", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A comment to store the name of the person or organization\n"
  "                             to which you're sending the transaction. This is not part of the \n"
  "                             transaction, just kept in your wallet."},
          {"subtractfeefromamount", RPCArg::Type::BOOL, /* default */ "false", "The fee will be deducted from the amount being sent.\n"
  "                             The recipient will receive less coins than you enter in the amount field."},
          {"replaceable", RPCArg::Type::BOOL, /* default */ "fallback to wallet's default", "Allow this transaction to be replaced by a transaction with higher fees via BIP 125"},
          {"conf_target", RPCArg::Type::NUM, /* default */ "fallback to wallet's default", "Confirmation target (in blocks)"},
          {"estimate_mode", RPCArg::Type::STR, /* default */ "UNSET", "The fee estimate mode, must be one of:\n"
  "       \"UNSET\"\n"
  "       \"ECONOMICAL\"\n"
  "       \"CONSERVATIVE\""},
      },
          RPCResult{
      "\"txid\"                  (string) The transaction id.\n"
          },
          RPCExamples{
              HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1")
      + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"donation\" \"seans outpost\"")
      + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"\" \"\" true")
      + HelpExampleRpc ("sendtoname", "\"id/foobar\", 0.1, \"donation\", \"seans outpost\"")
          },
      }.Check (request);

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VNUM, UniValue::VSTR,
                 UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL,
                 UniValue::VNUM, UniValue::VSTR});

  if (::ChainstateActive ().IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Xaya is downloading blocks...");

  /* Make sure the results are valid at least up to the most recent block
     the user could have gotten from another RPC command prior to now.  */
  pwallet->BlockUntilSyncedToCurrentChain();

  auto locked_chain = pwallet->chain().lock();
  LOCK(pwallet->cs_wallet);

  /* sendtoname does not support an options argument (e.g. to override the
     configured name/value encodings).  That would just add to the already
     long list of rarely used arguments.  Also, this function is inofficially
     deprecated anyway, see
     https://github.com/namecoin/namecoin-core/issues/12.  */
  const UniValue NO_OPTIONS(UniValue::VOBJ);

  const valtype name = DecodeNameFromRPCOrThrow (request.params[0], NO_OPTIONS);

  CNameData data;
  if (!::ChainstateActive ().CoinsTip ().GetName (name, data))
    {
      std::ostringstream msg;
      msg << "name not found: " << EncodeNameForMessage (name);
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
      coin_control.m_confirm_target = ParseConfirmTarget(request.params[6], pwallet->chain().estimateMaxBlocks());
  }

  if (!request.params[7].isNull()) {
      if (!FeeModeFromString(request.params[7].get_str(),
          coin_control.m_fee_mode)) {
          throw JSONRPCError(RPC_INVALID_PARAMETER,
                             "Invalid estimate_mode parameter");
      }
  }

  EnsureWalletIsUnlocked(pwallet);

  CTransactionRef tx = SendMoneyToScript (*locked_chain, pwallet,
                                          data.getAddress (), nullptr,
                                          nAmount, fSubtractFeeFromAmount,
                                          coin_control, std::move(mapValue));
  return tx->GetHash ().GetHex ();
}
