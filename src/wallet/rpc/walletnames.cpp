// Copyright (c) 2014-2024 Daniel Kraft
// Copyright (c) 2021 yanmaani
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <coins.h>
#include <common/args.h>
#include <consensus/validation.h>
#include <crypto/hkdf_sha256_32.h>
#include <core_io.h>
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
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/names.h>
#include <txmempool.h>
#include <util/fees.h>
#include <util/moneystr.h>
#include <util/translation.h>
#include <util/vector.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/rpc/util.h>
#include <wallet/rpc/wallet.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>

#include <univalue.h>

#include <algorithm>
#include <memory>

namespace wallet
{

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
   * Returns the destination that should be used as destination.
   */
  CTxDestination getDest ();

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

CTxDestination DestinationAddressHelper::getDest ()
{
  if (overrideDest != nullptr)
    return *overrideDest;

  rdest.reset (new ReserveDestination (&wallet, wallet.m_default_address_type));
  const auto op_dest = rdest->GetReservedDestination (false);
  if (!op_dest)
    throw JSONRPCError (RPC_WALLET_KEYPOOL_RAN_OUT,
                        strprintf ("Failed to generate mining address: %s",
                                   util::ErrorString (op_dest).original));

  return *op_dest;
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
UniValue
SendNameOutput (const JSONRPCRequest& request,
                CWallet& wallet,
                const CTxDestination& dest, const CScript& nameOp,
                const CTxIn* nameInput, const UniValue& opt)
{
  RPCTypeCheckObj (opt,
    {
      {"sendCoins", UniValueType (UniValue::VOBJ)},
      {"burn", UniValueType (UniValue::VOBJ)},
    },
    true, false);

  auto& node = EnsureAnyNodeContext (request);
  if (wallet.GetBroadcastTransactions ())
    EnsureConnman (node);

  std::vector<CRecipient> vecSend;
  vecSend.push_back ({dest, NAME_LOCKED_AMOUNT, false, nameOp});

  if (opt.exists ("sendCoins"))
    for (const std::string& addr : opt["sendCoins"].getKeys ())
      {
        const CTxDestination coinDest = DecodeDestination (addr);
        if (!IsValidDestination (coinDest))
          throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY,
                              "Invalid address: " + addr);

        const CAmount nAmount = AmountFromValue (opt["sendCoins"][addr]);
        if (nAmount <= 0)
          throw JSONRPCError (RPC_TYPE_ERROR, "Invalid amount for send");

        vecSend.push_back ({coinDest, nAmount, false});
      }

  if (opt.exists ("burn"))
    for (const std::string& data : opt["burn"].getKeys ())
      {
        /* MAX_OP_RETURN_RELAY contains three extra bytes, for the opcodes
           it includes.  */
        const auto bytes = ToByteVector (data);
        if (bytes.size () > MAX_OP_RETURN_RELAY - 3)
          throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY,
                              "Burn data is too long: " + data);

        const CAmount nAmount = AmountFromValue (opt["burn"][data]);
        if (nAmount <= 0)
          throw JSONRPCError (RPC_TYPE_ERROR, "Invalid amount for burn");

        const CScript scr = CScript () << OP_RETURN << bytes;
        vecSend.push_back ({CNoDestination (scr), nAmount, false});
      }

  CCoinControl coinControl;
  return SendMoney (wallet, coinControl, nameInput, vecSend, {}, false);
}

} // anonymous namespace
/* ************************************************************************** */

RPCHelpMan
name_list ()
{
  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ();

  return RPCHelpMan ("name_list",
      "\nShows the status of all names in the wallet.\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Only include this name"},
          optHelp.buildRpcArg (),
      },
      RPCResult {RPCResult::Type::ARR, "", "",
          {
              NameInfoHelp ()
                .withHeight ()
                .finish ()
          }
      },
      RPCExamples {
          HelpExampleCli ("name_list", "")
        + HelpExampleCli ("name_list", "\"myname\"")
        + HelpExampleRpc ("name_list", "")
      },
      [&] (const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  if (!wallet)
    return NullUniValue;
  CWallet* const pwallet = wallet.get ();

  const auto& chainman = EnsureChainman (EnsureAnyNodeContext (request));

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
  LOCK2 (pwallet->cs_wallet, cs_main);

  const int tipHeight = chainman.ActiveHeight ();
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

      const int depth = pwallet->GetTxDepthInMainChain(tx);
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
  );
}

/**
 * Generate a salt using HKDF for a given name + private key combination.
 * Indirectly used in name_new and name_firstupdate.
 * Refactored out to make testing easier.
 * @return True on success, false otherwise
 */
bool
getNameSalt(const CKey& key, const valtype& name, valtype& rand)
{
    const valtype ikm(reinterpret_cast<const unsigned char*> (key.begin()),
                      reinterpret_cast<const unsigned char*> (key.end()));
    const std::string salt(reinterpret_cast<const char*>(name.data()), name.size());
    const std::string info("Namecoin Registration Salt");
    CHKDF_HMAC_SHA256_L32 hkdf32(ikm.data(), ikm.size(), salt);
    unsigned char tmp[32];
    hkdf32.Expand32(info, tmp);

    rand = valtype(tmp, tmp + 20);
    return true;
}

namespace
{

/**
 * Generate a salt using HKDF for a given name + txout combination.
 * Used in name_new and name_firstupdate.
 * @return True on success, false otherwise
 */
bool
getNameSalt(CWallet* const pwallet, const valtype& name, const CScript& output, valtype& rand)
{
    AssertLockHeld(pwallet->cs_wallet);

    for (const auto* spk : pwallet->GetScriptPubKeyMans (output))
      {
        if (spk == nullptr)
            continue;

        auto provider = spk->GetSigningProviderWithKeys (output);
        if (provider == nullptr)
            continue;

        CTxDestination dest;
        CKeyID keyid;
        CKey key;
        if (!ExtractDestination(output, dest))
            continue; // If multisig.
        assert(IsValidDestination(dest)); // We should never get a null destination.

        keyid = GetKeyForDestination(*provider, dest);
        provider->GetKey(keyid, key);

        return wallet::getNameSalt(key, name, rand);
      }

    return false;
}

bool
saltMatchesHash(const valtype& name, const valtype& rand, const valtype& expectedHash)
{
    valtype toHash(rand);
    toHash.insert (toHash.end(), name.begin(), name.end());

    return (Hash160(toHash) == uint160(expectedHash));
}

} // anonymous namespace

/* ************************************************************************** */

RPCHelpMan
name_register ()
{
  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ()
      .withWriteOptions ();

  return RPCHelpMan ("name_register",
      "\nRegisters a new name."
          + HELP_REQUIRING_PASSPHRASE,
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to register"},
          {"value", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Value for the name"},
          optHelp.buildRpcArg (),
      },
      RPCResult {RPCResult::Type::STR_HEX, "", "the transaction ID"},
      RPCExamples {
          HelpExampleCli ("name_register", "\"myname\", \"new-value\"")
        + HelpExampleRpc ("name_register", "\"myname\", \"new-value\"")
      },
      [&] (const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  if (!wallet)
    return NullUniValue;
  CWallet* const pwallet = wallet.get ();

  const auto& node = EnsureAnyNodeContext (request);
  const auto& chainman = EnsureChainman (node);

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 3)
    options = request.params[2].get_obj ();

  const valtype name = DecodeNameFromRPCOrThrow (request.params[0], options);
  TxValidationState state;
  if (!IsNameValid (name, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  const bool isDefaultVal = (request.params.size () < 2 || request.params[1].isNull ());
  const valtype value = isDefaultVal ?
      valtype ({'{', '}'}):
      DecodeValueFromRPCOrThrow (request.params[1], options);

  if (!IsValueValid (value, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  /* Reject updates to a name for which the mempool already has
     a pending registration.  This is not a hard rule enforced by network
     rules, but it is necessary with the current mempool implementation.  */
  {
    auto& mempool = EnsureMemPool (node);
    LOCK (mempool.cs);
    if (mempool.registersName (name))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "there is already a pending registration"
                          " for this name");
  }

  {
    LOCK (cs_main);
    CNameData data;
    if (chainman.ActiveChainstate ().CoinsTip ().GetName (name, data))
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "this name exists already");
  }

  /* Make sure the results are valid at least up to the most recent block
     the user could have gotten from another RPC command prior to now.  */
  pwallet->BlockUntilSyncedToCurrentChain ();

  LOCK (pwallet->cs_wallet);

  EnsureWalletIsUnlocked (*pwallet);

  DestinationAddressHelper destHelper(*pwallet);
  destHelper.setOptions (options);

  const CScript nameOp
    = CNameScript::buildNameRegister (CScript (), name, value);

  const UniValue txidVal
      = SendNameOutput (request, *pwallet, destHelper.getDest (), nameOp,
                        nullptr, options);
  destHelper.finalise ();

  return txidVal;
}
  );
}

/* ************************************************************************** */

RPCHelpMan
name_update ()
{
  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ()
      .withWriteOptions ();

  return RPCHelpMan ("name_update",
      "\nUpdates a name and possibly transfers it."
          + HELP_REQUIRING_PASSPHRASE,
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to update"},
          {"value", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Value for the name"},
          optHelp.buildRpcArg (),
      },
      RPCResult {RPCResult::Type::STR_HEX, "", "the transaction ID"},
      RPCExamples {
          HelpExampleCli ("name_update", "\"myname\", \"new-value\"")
        + HelpExampleRpc ("name_update", "\"myname\", \"new-value\"")
      },
      [&] (const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  if (!wallet)
    return NullUniValue;
  CWallet* const pwallet = wallet.get ();

  const auto& node = EnsureAnyNodeContext (request);
  const auto& chainman = EnsureChainman (node);

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 3)
    options = request.params[2].get_obj ();

  const valtype name = DecodeNameFromRPCOrThrow (request.params[0], options);
  TxValidationState state;
  if (!IsNameValid (name, state))
    throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());

  const bool isDefaultVal = request.params.size() < 2 || request.params[1].isNull();

  valtype value;
  if (!isDefaultVal) {
      value = DecodeValueFromRPCOrThrow (request.params[1], options);
      if (!IsValueValid (value, state))
        throw JSONRPCError (RPC_INVALID_PARAMETER, state.GetRejectReason ());
  }

  /* For finding the name output to spend and its value, we first check if
     there are pending operations on the name in the mempool.  If there
     are, then we build upon the last one to get a valid chain.  If there
     are none, then we look up the last outpoint from the name database
     instead. */
  // TODO: Use name_show for this instead.

  const unsigned chainLimit = gArgs.GetIntArg ("-limitnamechains",
                                               DEFAULT_NAME_CHAIN_LIMIT);
  COutPoint outp;
  {
    auto& mempool = EnsureMemPool (node);
    LOCK (mempool.cs);

    const unsigned pendingOps = mempool.pendingNameChainLength (name);
    if (pendingOps >= chainLimit)
      throw JSONRPCError (RPC_TRANSACTION_ERROR,
                          "there are already too many pending operations"
                          " on this name");

    if (pendingOps > 0)
      {
        outp = mempool.lastNameOutput (name);
        if (isDefaultVal)
          {
            const auto& tx = mempool.mapTx.find(outp.hash)->GetTx();
            value = CNameScript(tx.vout[outp.n].scriptPubKey).getOpValue();
          }
      }
  }

  if (outp.IsNull ())
    {
      LOCK (cs_main);

      CNameData oldData;
      const auto& coinsTip = chainman.ActiveChainstate ().CoinsTip ();
      if (!coinsTip.GetName (name, oldData))
        throw JSONRPCError (RPC_TRANSACTION_ERROR,
                            "this name can not be updated");
      if (isDefaultVal)
        value = oldData.getValue();
      outp = oldData.getUpdateOutpoint ();
    }
  assert (!outp.IsNull ());
  const CTxIn txIn(outp);

  /* Make sure the results are valid at least up to the most recent block
     the user could have gotten from another RPC command prior to now.  */
  pwallet->BlockUntilSyncedToCurrentChain ();

  LOCK (pwallet->cs_wallet);

  EnsureWalletIsUnlocked (*pwallet);

  DestinationAddressHelper destHelper(*pwallet);
  destHelper.setOptions (options);

  const CScript nameOp
    = CNameScript::buildNameUpdate (CScript (), name, value);

  const UniValue txidVal
      = SendNameOutput (request, *pwallet, destHelper.getDest (), nameOp,
                        &txIn, options);
  destHelper.finalise ();

  return txidVal;
}
  );
}

/* ************************************************************************** */

RPCHelpMan
queuerawtransaction ()
{
  return RPCHelpMan ("queuerawtransaction",
      "\nQueue a transaction for future broadcast.",
      {
          {"hexstring", RPCArg::Type::STR, RPCArg::Optional::NO, "The hex string of the raw transaction"},
      },
      RPCResult {RPCResult::Type::STR_HEX, "", "the transaction ID"},
      RPCExamples {
          HelpExampleCli("queuerawtransaction", "txhex") +
          HelpExampleRpc("queuerawtransaction", "txhex")
      },
      [&] (const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  if (!wallet) return NullUniValue;

  // parse transaction from parameter
  CMutableTransaction mtxParsed;
  if (!DecodeHexTx(mtxParsed, request.params[0].get_str(), true, true))
    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
  CTransactionRef txParsed(MakeTransactionRef(mtxParsed));
  const uint256& hashTx = txParsed->GetHash();

  // Validate transaction
  node::NodeContext& node = EnsureAnyNodeContext(request);
  ChainstateManager& chainman = EnsureChainman(node);

  {
    LOCK (cs_main);
    // Check validity
    MempoolAcceptResult result = AcceptToMemoryPool(chainman.ActiveChainstate(), txParsed,
      GetTime(), /* bypass_limits */ false, /* test_accept */ true);
    // If it can be broadcast immediately, do that and return early.
    if (result.m_result_type == MempoolAcceptResult::ResultType::VALID)
    {
      std::string unused_err_string;
      // Don't check max fee.
      const TransactionError err = BroadcastTransaction(node, txParsed, unused_err_string,
        /* max_tx_fee */ 0, /* relay */ true, /* wait_callback */ false);
      assert(err == TransactionError::OK);

      return hashTx.GetHex();
    }

    // Otherwise, it's not valid right now.
    if (result.m_state.GetResult() == TxValidationResult::TX_CONSENSUS)
        /* We only want to avoid unconditionally invalid transactions.
         * Blocking e.g. orphan transactions is not desirable. */
      throw JSONRPCError (RPC_WALLET_ERROR, strprintf("Invalid transaction (%s)", result.m_state.GetRejectReason()));
  }

  // After these checks, add it to the queue.
  {
    LOCK (wallet->cs_wallet);
    if (!wallet->WriteQueuedTransaction(hashTx, mtxParsed))
    {
      throw JSONRPCError (RPC_WALLET_ERROR, "Error queueing transaction");
    }
  }

  return hashTx.GetHex();
}
  );
}

/* ************************************************************************** */

RPCHelpMan
dequeuetransaction ()
{
  return RPCHelpMan ("dequeuetransaction",
      "\nRemove a transaction from the queue.",
      {
          {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction ID of the transaction to be dequeued"},
      },
      RPCResult {RPCResult::Type::NONE, "", ""},
      RPCExamples {
          HelpExampleCli("dequeuetransaction", "txid") +
          HelpExampleRpc("dequeuetransaction", "txid")
      },
      [&] (const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  if (!wallet) return NullUniValue;

  const uint256& txid = ParseHashV (request.params[0], "txid");

  LOCK (wallet->cs_wallet);

  if (!wallet->EraseQueuedTransaction(txid))
  {
    throw JSONRPCError (RPC_WALLET_ERROR, "Error dequeueing transaction");
  }

  return NullUniValue;
}
  );
}

/* ************************************************************************** */

RPCHelpMan
listqueuedtransactions ()
{
  return RPCHelpMan{"listqueuedtransactions",
      "\nList the transactions that are queued for future broadcast.\n",
      {
      },
      RPCResult{
          RPCResult::Type::OBJ_DYN, "", "JSON object with transaction ID's as keys",
          {
              {RPCResult::Type::OBJ, "", "",
              {
                  {RPCResult::Type::STR_HEX, "transaction", "The hex string of the raw transaction."},
              }},
          }
      },
      RPCExamples{
          HelpExampleCli("listqueuedtransactions", "") +
          HelpExampleRpc("listqueuedtransactions", "")
      },
      [&] (const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  if (!wallet) return NullUniValue;

  LOCK (wallet->cs_wallet);

  UniValue result(UniValue::VOBJ);

  for (const auto& i : wallet->queuedTransactionMap)
  {
    const uint256& txid = i.first;
    const CMutableTransaction& tx = i.second;

    const std::string txStr = EncodeHexTx(CTransaction(tx));

    UniValue entry(UniValue::VOBJ);
    entry.pushKV("transaction", txStr);

    result.pushKV(txid.GetHex(), entry);
  }

  return result;
}
  };
}

/* ************************************************************************** */

RPCHelpMan
sendtoname ()
{
  return RPCHelpMan{"sendtoname",
      "\nSend an amount to the owner of a name.\n"
      "\nIt is an error if the name is expired."
          + HELP_REQUIRING_PASSPHRASE,
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to send to."},
          {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount in " + CURRENCY_UNIT + " to send. eg 0.1"},
          {"comment", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "A comment used to store what the transaction is for.\n"
  "                             This is not part of the transaction, just kept in your wallet."},
          {"comment_to", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "A comment to store the name of the person or organization\n"
  "                             to which you're sending the transaction. This is not part of the \n"
  "                             transaction, just kept in your wallet."},
          {"subtractfeefromamount", RPCArg::Type::BOOL, RPCArg::Default{false}, "The fee will be deducted from the amount being sent.\n"
  "                             The recipient will receive less coins than you enter in the amount field."},
          {"replaceable", RPCArg::Type::BOOL, RPCArg::DefaultHint{"fallback to wallet's default"}, "Allow this transaction to be replaced by a transaction with higher fees via BIP 125"},
      },
          RPCResult {RPCResult::Type::STR_HEX, "", "the transaction ID"},
          RPCExamples{
              HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1")
      + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"donation\" \"seans outpost\"")
      + HelpExampleCli ("sendtoname", "\"id/foobar\" 0.1 \"\" \"\" true")
      + HelpExampleRpc ("sendtoname", "\"id/foobar\", 0.1, \"donation\", \"seans outpost\"")
          },
      [&] (const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
  std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest (request);
  if (!pwallet)
    return NullUniValue;
  const auto& chainman = EnsureChainman (EnsureAnyNodeContext (request));

  /* Make sure the results are valid at least up to the most recent block
     the user could have gotten from another RPC command prior to now.  */
  pwallet->BlockUntilSyncedToCurrentChain();

  LOCK(pwallet->cs_wallet);

  /* sendtoname does not support an options argument (e.g. to override the
     configured name/value encodings).  That would just add to the already
     long list of rarely used arguments.  Also, this function is inofficially
     deprecated anyway, see
     https://github.com/namecoin/namecoin-core/issues/12.  */
  const UniValue NO_OPTIONS(UniValue::VOBJ);

  const valtype name = DecodeNameFromRPCOrThrow (request.params[0], NO_OPTIONS);

  CNameData data;
  {
    LOCK (cs_main);
    if (!chainman.ActiveChainstate ().CoinsTip ().GetName (name, data))
      {
        std::ostringstream msg;
        msg << "name not found: " << EncodeNameForMessage (name);
        throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, msg.str ());
      }
  }

  /* The code below is strongly based on sendtoaddress.  Make sure to
     keep it in sync.  */

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

  EnsureWalletIsUnlocked(*pwallet);

  std::vector<CRecipient> recipients;
  const CNoDestination dest(data.getAddress ());
  const CAmount amount = AmountFromValue (request.params[1]);
  recipients.push_back ({dest, amount, fSubtractFeeFromAmount});

  return SendMoney(*pwallet, coin_control, nullptr, recipients, mapValue, false);
}
  };
}

} // namespace wallet
