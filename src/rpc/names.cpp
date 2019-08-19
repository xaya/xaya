// Copyright (c) 2014-2019 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <chainparams.h>
#include <core_io.h>
#include <init.h>
#include <key_io.h>
#include <names/common.h>
#include <names/main.h>
#include <primitives/transaction.h>
#include <rpc/names.h>
#include <rpc/server.h>
#include <script/names.h>
#include <txmempool.h>
#include <util/strencodings.h>
#include <validation.h>
#ifdef ENABLE_WALLET
# include <wallet/rpcwallet.h>
# include <wallet/wallet.h>
#endif

#include <univalue.h>

#include <boost/xpressive/xpressive_dynamic.hpp>

#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>

namespace
{

NameEncoding
EncodingFromOptionsJson (const UniValue& options, const std::string& field,
                         const NameEncoding defaultValue)
{
  NameEncoding res = defaultValue;
  RPCTypeCheckObj (options,
    {
      {field, UniValueType (UniValue::VSTR)},
    },
    true, false);
  if (options.exists (field))
    try
      {
        res = EncodingFromString (options[field].get_str ());
      }
    catch (const std::invalid_argument& exc)
      {
        LogPrintf ("Invalid value for %s in options: %s\n  using default %s\n",
                   field, exc.what (), EncodingToString (defaultValue));
      }

  return res;
}

} // anonymous namespace

/**
 * Utility routine to construct a "name info" object to return.  This is used
 * for name_show and also name_list.
 */
UniValue
getNameInfo (const UniValue& options,
             const valtype& name, const valtype& value,
             const COutPoint& outp, const CScript& addr)
{
  UniValue obj(UniValue::VOBJ);
  AddEncodedNameToUniv (obj, "name", name,
                        EncodingFromOptionsJson (options, "nameEncoding",
                                                 ConfiguredNameEncoding ()));
  AddEncodedNameToUniv (obj, "value", value,
                        EncodingFromOptionsJson (options, "valueEncoding",
                                                 ConfiguredValueEncoding ()));
  obj.pushKV ("txid", outp.hash.GetHex ());
  obj.pushKV ("vout", static_cast<int> (outp.n));

  /* Try to extract the address.  May fail if we can't parse the script
     as a "standard" script.  */
  CTxDestination dest;
  std::string addrStr;
  if (ExtractDestination (addr, dest))
    addrStr = EncodeDestination (dest);
  else
    addrStr = "<nonstandard>";
  obj.pushKV ("address", addrStr);

  return obj;
}

/**
 * Return name info object for a CNameData object.
 */
UniValue
getNameInfo (const UniValue& options,
             const valtype& name, const CNameData& data)
{
  UniValue result = getNameInfo (options,
                                 name, data.getValue (),
                                 data.getUpdateOutpoint (),
                                 data.getAddress ());
  addHeightInfo (data.getHeight (), result);
  return result;
}

void
addHeightInfo (const int height, UniValue& data)
{
  data.pushKV ("height", height);
}

#ifdef ENABLE_WALLET
/**
 * Adds the "ismine" field giving ownership info to the JSON object.
 */
void
addOwnershipInfo (const CScript& addr, const CWallet* pwallet,
                  UniValue& data)
{
  if (pwallet == nullptr)
    return;

  AssertLockHeld (pwallet->cs_wallet);
  const isminetype mine = IsMine (*pwallet, addr);
  const bool isMine = (mine & ISMINE_SPENDABLE);
  data.pushKV ("ismine", isMine);
}
#endif

namespace
{

valtype
DecodeNameValueFromRPCOrThrow (const UniValue& val, const UniValue& opt,
                               const std::string& optKey,
                               const NameEncoding defaultEnc)
{
  const NameEncoding enc = EncodingFromOptionsJson (opt, optKey, defaultEnc);
  try
    {
      return DecodeName (val.get_str (), enc);
    }
  catch (const InvalidNameString& exc)
    {
      std::ostringstream msg;
      msg << "Name/value is invalid for encoding " << EncodingToString (enc);
      throw JSONRPCError (RPC_NAME_INVALID_ENCODING, msg.str ());
    }
}

} // anonymous namespace

valtype
DecodeNameFromRPCOrThrow (const UniValue& val, const UniValue& opt)
{
  return DecodeNameValueFromRPCOrThrow (val, opt, "nameEncoding",
                                        ConfiguredNameEncoding ());
}

valtype
DecodeValueFromRPCOrThrow (const UniValue& val, const UniValue& opt)
{
  return DecodeNameValueFromRPCOrThrow (val, opt, "valueEncoding",
                                        ConfiguredValueEncoding ());
}

namespace
{

/**
 * Helper class that extracts the wallet for the current RPC request, if any.
 * It handles the case of disabled wallet support or no wallet being present,
 * so that it is suitable for the non-wallet RPCs here where we just want to
 * provide optional extra features (like the "ismine" field).
 *
 * The main benefit of having this class is that we can easily LOCK2 with the
 * wallet and another lock we need, without having to care about the special
 * cases where no wallet is present or wallet support is disabled.
 */
class MaybeWalletForRequest
{

private:

#ifdef ENABLE_WALLET
  std::shared_ptr<CWallet> wallet;
#endif

public:

  explicit MaybeWalletForRequest (const JSONRPCRequest& request)
  {
#ifdef ENABLE_WALLET
    wallet = GetWalletForJSONRPCRequest (request);
#endif
  }

  CCriticalSection*
  getLock () const
  {
#ifdef ENABLE_WALLET
    return (wallet != nullptr ? &wallet->cs_wallet : nullptr);
#else
    return nullptr;
#endif
  }

#ifdef ENABLE_WALLET
  CWallet*
  getWallet ()
  {
    return wallet.get ();
  }

  const CWallet*
  getWallet () const
  {
    return wallet.get ();
  }
#endif

};

/**
 * Variant of addOwnershipInfo that uses a MaybeWalletForRequest.  This takes
 * care of disabled wallet support.
 */
void
addOwnershipInfo (const CScript& addr, const MaybeWalletForRequest& wallet,
                  UniValue& data)
{
#ifdef ENABLE_WALLET
  addOwnershipInfo (addr, wallet.getWallet (), data);
#endif
}

/**
 * Utility variant of getNameInfo that already includes ownership information.
 * This is the most common call for methods in this file.
 */
UniValue
getNameInfo (const UniValue& options,
             const valtype& name, const CNameData& data,
             const MaybeWalletForRequest& wallet)
{
  UniValue res = getNameInfo (options, name, data);
  addOwnershipInfo (data.getAddress (), wallet, res);
  return res;
}

} // anonymous namespace

/* ************************************************************************** */

HelpTextBuilder::HelpTextBuilder (const std::string& ind, const size_t col)
  : indent(ind), docColumn(col)
{
  result << indent << "{" << std::endl;
}

std::string
HelpTextBuilder::finish (const std::string& trailing)
{
  result << indent << "}" << trailing << std::endl;
  return result.str ();
}

HelpTextBuilder&
HelpTextBuilder::withLine (const std::string& line)
{
  result << indent << "  " << line << std::endl;
  return *this;
}

HelpTextBuilder&
HelpTextBuilder::withField (const std::string& field, const std::string& doc)
{
  return withField (field, ",", doc);
}

HelpTextBuilder&
HelpTextBuilder::withField (const std::string& field,
                            const std::string& delim, const std::string& doc)
{
  assert (field.size () < docColumn);

  result << indent << "  " << field << delim;
  result << std::string (docColumn - field.size (), ' ') << doc << std::endl;

  return *this;
}

NameInfoHelp::NameInfoHelp (const std::string& ind)
  : HelpTextBuilder(ind, 25)
{
  withField ("\"name\": xxxxx", "(string) the requested name");
  withField ("\"name_encoding\": xxxxx", "(string) the encoding of \"name\"");
  withField ("\"name_error\": xxxxx",
             "(string) replaces \"name\" in case there is an error");
  withField ("\"value\": xxxxx", "(string) the name's current value");
  withField ("\"value_encoding\": xxxxx", "(string) the encoding of \"value\"");
  withField ("\"value_error\": xxxxx",
             "(string) replaces \"value\" in case there is an error");

  withField ("\"txid\": xxxxx", "(string) the name's last update tx");
  withField ("\"vout\": xxxxx",
           "(numeric) the index of the name output in the last update");
  withField ("\"address\": xxxxx", "(string) the address holding the name");
#ifdef ENABLE_WALLET
  withField ("\"ismine\": xxxxx",
             "(boolean) whether the name is owned by the wallet");
#endif
}

NameInfoHelp&
NameInfoHelp::withHeight ()
{
  withField ("\"height\": xxxxx", "(numeric) the name's last update height");
  return *this;
}

NameOptionsHelp::NameOptionsHelp ()
{}

NameOptionsHelp&
NameOptionsHelp::withArg (const std::string& name, const RPCArg::Type type,
                          const std::string& doc)
{
  return withArg (name, type, "", doc);
}

NameOptionsHelp&
NameOptionsHelp::withArg (const std::string& name, const RPCArg::Type type,
                          const std::string& defaultValue,
                          const std::string& doc)
{
  if (defaultValue.empty ())
    innerArgs.push_back (RPCArg (name, type, RPCArg::Optional::OMITTED, doc));
  else
    innerArgs.push_back (RPCArg (name, type, defaultValue, doc));

  return *this;
}

NameOptionsHelp&
NameOptionsHelp::withWriteOptions ()
{
  withArg ("destAddress", RPCArg::Type::STR,
           "The address to send the name output to");

  withArg ("sendCoins", RPCArg::Type::OBJ_USER_KEYS,
           "Addresses to which coins should be sent additionally");

  return *this;
}

NameOptionsHelp&
NameOptionsHelp::withNameEncoding ()
{
  withArg ("nameEncoding", RPCArg::Type::STR,
           "Encoding (\"ascii\", \"utf8\" or \"hex\") of the name argument");
  return *this;
}

NameOptionsHelp&
NameOptionsHelp::withValueEncoding ()
{
  withArg ("valueEncoding", RPCArg::Type::STR,
           "Encoding (\"ascii\", \"utf8\" or \"hex\") of the value argument");
  return *this;
}

RPCArg
NameOptionsHelp::buildRpcArg () const
{
  return RPCArg ("options", RPCArg::Type::OBJ,
                 RPCArg::Optional::OMITTED_NAMED_ARG,
                 "Options for this RPC call",
                 innerArgs, "options");
}

/* ************************************************************************** */
namespace
{

UniValue
name_show (const JSONRPCRequest& request)
{
  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ();

  RPCHelpMan ("name_show",
      "\nLooks up the current data for the given name.  Fails if the name doesn't exist.\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to query for"},
          optHelp.buildRpcArg (),
      },
      RPCResult {
        NameInfoHelp ("")
          .withHeight ()
          .finish (""),
      },
      RPCExamples {
          HelpExampleCli ("name_show", "\"myname\"")
        + HelpExampleRpc ("name_show", "\"myname\"")
      }
  ).Check (request);

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VOBJ});

  if (::ChainstateActive ().IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Xaya is downloading blocks...");

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 2)
    options = request.params[1].get_obj ();

  const valtype name
      = DecodeNameFromRPCOrThrow (request.params[0], options);

  CNameData data;
  {
    LOCK (cs_main);
    if (!::ChainstateActive ().CoinsTip ().GetName (name, data))
      {
        std::ostringstream msg;
        msg << "name not found: " << EncodeNameForMessage (name);
        throw JSONRPCError (RPC_WALLET_ERROR, msg.str ());
      }
  }

  MaybeWalletForRequest wallet(request);
  LOCK (wallet.getLock ());
  return getNameInfo (options, name, data, wallet);
}

/* ************************************************************************** */

UniValue
name_history (const JSONRPCRequest& request)
{
  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ();

  RPCHelpMan ("name_history",
      "\nLooks up the current and all past data for the given name.  -namehistory must be enabled.\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to query for"},
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
          HelpExampleCli ("name_history", "\"myname\"")
        + HelpExampleRpc ("name_history", "\"myname\"")
      }
  ).Check (request);

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VOBJ});

  if (!fNameHistory)
    throw std::runtime_error ("-namehistory is not enabled");

  if (::ChainstateActive ().IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Xaya is downloading blocks...");

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 2)
    options = request.params[1].get_obj ();

  const valtype name
      = DecodeNameFromRPCOrThrow (request.params[0], options);

  CNameData data;
  CNameHistory history;

  {
    LOCK (cs_main);

    const auto& coinsTip = ::ChainstateActive ().CoinsTip ();
    if (!coinsTip.GetName (name, data))
      {
        std::ostringstream msg;
        msg << "name not found: " << EncodeNameForMessage (name);
        throw JSONRPCError (RPC_WALLET_ERROR, msg.str ());
      }

    if (!coinsTip.GetNameHistory (name, history))
      assert (history.empty ());
  }

  MaybeWalletForRequest wallet(request);
  LOCK (wallet.getLock ());

  UniValue res(UniValue::VARR);
  for (const auto& entry : history.getData ())
    res.push_back (getNameInfo (options, name, entry, wallet));
  res.push_back (getNameInfo (options, name, data, wallet));

  return res;
}

/* ************************************************************************** */

UniValue
name_scan (const JSONRPCRequest& request)
{
  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ()
      .withArg ("minConf", RPCArg::Type::NUM, "1",
                "Minimum number of confirmations")
      .withArg ("maxConf", RPCArg::Type::NUM,
                "Maximum number of confirmations")
      .withArg ("prefix", RPCArg::Type::STR,
                "Filter for names with the given prefix")
      .withArg ("regexp", RPCArg::Type::STR,
                "Filter for names matching the regexp");

  RPCHelpMan ("name_scan",
      "\nLists names in the database.\n",
      {
          {"start", RPCArg::Type::STR, "", "Skip initially to this name"},
          {"count", RPCArg::Type::NUM, "500", "Stop after this many names"},
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
          HelpExampleCli ("name_scan", "")
        + HelpExampleCli ("name_scan", "\"d/abc\"")
        + HelpExampleCli ("name_scan", "\"d/abc\" 10")
        + HelpExampleRpc ("name_scan", "\"d/abc\"")
      }
  ).Check (request);

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ});

  if (::ChainstateActive ().IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Xaya is downloading blocks...");

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 3)
    options = request.params[2].get_obj ();

  valtype start;
  if (request.params.size () >= 1)
    start = DecodeNameFromRPCOrThrow (request.params[0], options);

  int count = 500;
  if (request.params.size () >= 2)
    count = request.params[1].get_int ();

  /* Parse and interpret the name_scan-specific options.  */
  RPCTypeCheckObj (options,
    {
      {"minConf", UniValueType (UniValue::VNUM)},
      {"maxConf", UniValueType (UniValue::VNUM)},
      {"prefix", UniValueType (UniValue::VSTR)},
      {"regexp", UniValueType (UniValue::VSTR)},
    },
    true, false);

  int minConf = 1;
  if (options.exists ("minConf"))
    minConf = options["minConf"].get_int ();
  if (minConf < 1)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "minConf must be >= 1");

  int maxConf = -1;
  if (options.exists ("maxConf"))
    {
      maxConf = options["maxConf"].get_int ();
      if (maxConf < 0)
        throw JSONRPCError (RPC_INVALID_PARAMETER,
                            "maxConf must not be negative");
    }

  valtype prefix;
  if (options.exists ("prefix"))
    prefix = DecodeNameFromRPCOrThrow (options["prefix"], options);

  bool haveRegexp = false;
  boost::xpressive::sregex regexp;
  if (options.exists ("regexp"))
    {
      haveRegexp = true;
      regexp = boost::xpressive::sregex::compile (options["regexp"].get_str ());
    }

  /* Iterate over names and produce the result.  */
  UniValue res(UniValue::VARR);
  if (count <= 0)
    return res;

  MaybeWalletForRequest wallet(request);
  LOCK2 (cs_main, wallet.getLock ());

  const int maxHeight = ::ChainActive ().Height () - minConf + 1;
  int minHeight = -1;
  if (maxConf >= 0)
    minHeight = ::ChainActive ().Height () - maxConf + 1;

  valtype name;
  CNameData data;
  const auto& coinsTip = ::ChainstateActive ().CoinsTip ();
  std::unique_ptr<CNameIterator> iter(coinsTip.IterateNames ());
  for (iter->seek (start); count > 0 && iter->next (name, data); )
    {
      const int height = data.getHeight ();
      if (height > maxHeight)
        continue;
      if (minHeight >= 0 && height < minHeight)
        continue;

      if (name.size () < prefix.size ())
        continue;
      if (!std::equal (prefix.begin (), prefix.end (), name.begin ()))
        continue;

      if (haveRegexp)
        {
          try
            {
              const std::string nameStr = EncodeName (name, NameEncoding::UTF8);
              boost::xpressive::smatch matches;
              if (!boost::xpressive::regex_search (nameStr, matches, regexp))
                continue;
            }
          catch (const InvalidNameString& exc)
            {
              continue;
            }
        }

      res.push_back (getNameInfo (options, name, data, wallet));
      --count;
    }

  return res;
}

/* ************************************************************************** */

UniValue
name_pending (const JSONRPCRequest& request)
{
  NameOptionsHelp optHelp;
  optHelp
      .withNameEncoding ()
      .withValueEncoding ();

  RPCHelpMan ("name_pending",
      "\nLists unconfirmed name operations in the mempool.\n"
      "\nIf a name is given, only check for operations on this name.\n",
      {
          {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Only look for this name"},
          optHelp.buildRpcArg (),
      },
      RPCResult {
        "[\n"
        + NameInfoHelp ("  ")
            .withField ("\"op\": xxxxx",
                        "(string) the operation being performed")
            .finish (",") +
        "  ...\n"
        "]\n"
      },
      RPCExamples {
          HelpExampleCli ("name_pending", "")
        + HelpExampleCli ("name_pending", "\"d/domob\"")
        + HelpExampleRpc ("name_pending", "")
      }
  ).Check (request);

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VOBJ}, true);

  MaybeWalletForRequest wallet(request);
  LOCK2 (wallet.getLock (), mempool.cs);

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 2)
    options = request.params[1].get_obj ();

  std::vector<uint256> txHashes;
  mempool.queryHashes (txHashes);

  const bool hasNameFilter = !request.params[0].isNull ();
  valtype nameFilter;
  if (hasNameFilter)
    nameFilter = DecodeNameFromRPCOrThrow (request.params[0], options);

  UniValue arr(UniValue::VARR);
  for (const auto& txHash : txHashes)
    {
      std::shared_ptr<const CTransaction> tx = mempool.get (txHash);
      if (!tx)
        continue;

      for (size_t n = 0; n < tx->vout.size (); ++n)
        {
          const auto& txOut = tx->vout[n];
          const CNameScript op(txOut.scriptPubKey);
          if (!op.isNameOp () || !op.isAnyUpdate ())
            continue;
          if (hasNameFilter && op.getOpName () != nameFilter)
            continue;

          UniValue obj = getNameInfo (options,
                                      op.getOpName (), op.getOpValue (),
                                      COutPoint (tx->GetHash (), n),
                                      op.getAddress ());
          addOwnershipInfo (op.getAddress (), wallet, obj);
          switch (op.getNameOp ())
            {
            case OP_NAME_REGISTER:
              obj.pushKV ("op", "name_register");
              break;
            case OP_NAME_UPDATE:
              obj.pushKV ("op", "name_update");
              break;
            default:
              assert (false);
            }

          arr.push_back (obj);
        }
    }

  return arr;
}

/* ************************************************************************** */

UniValue
namerawtransaction (const JSONRPCRequest& request)
{
  RPCHelpMan ("namerawtransaction",
      "\nAdds a name operation to an existing raw transaction.\n"
      "\nUse createrawtransaction first to create the basic transaction, including the required inputs and outputs also for the name.\n",
      {
          {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction hex string"},
          {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The vout of the desired name output"},
          {"nameop", RPCArg::Type::OBJ, RPCArg::Optional::NO, "The name operation to create",
              {
                  {"op", RPCArg::Type::STR, RPCArg::Optional::NO, "The operation to perform, can be \"name_new\", \"name_firstupdate\" and \"name_update\""},
                  {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name to operate on"},
                  {"value", RPCArg::Type::STR, RPCArg::Optional::NO, "The new value for the name"},
              },
           "nameop"},
      },
      RPCResult {
        "{\n"
        "  \"hex\": xxx,        (string) Hex string of the updated transaction\n"
        "}\n"
      },
      RPCExamples {
          HelpExampleCli ("namerawtransaction", R"("raw tx hex" 1 "{\"op\":\"name_register\",\"name\":\"my-name\",\"value\":\"new value\")")
        + HelpExampleCli ("namerawtransaction", R"("raw tx hex" 1 "{\"op\":\"name_update\",\"name\":\"my-name\",\"value\":\"new value\")")
        + HelpExampleRpc ("namerawtransaction", R"("raw tx hex", 1, "{\"op\":\"name_update\",\"name\":\"my-name\",\"value\":\"new value\")")
      }
  ).Check (request);

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ});

  CMutableTransaction mtx;
  if (!DecodeHexTx (mtx, request.params[0].get_str (), true))
    throw JSONRPCError (RPC_DESERIALIZATION_ERROR, "TX decode failed");

  const size_t nOut = request.params[1].get_int ();
  if (nOut >= mtx.vout.size ())
    throw JSONRPCError (RPC_INVALID_PARAMETER, "vout is out of range");

  /* namerawtransaction does not have an options argument.  This would just
     make the already long list of arguments longer.  Instead of using
     namerawtransaction, namecoin-tx can be used anyway to create name
     operations with arbitrary hex data.  */
  const UniValue NO_OPTIONS(UniValue::VOBJ);

  const UniValue nameOp = request.params[2].get_obj ();
  RPCTypeCheckObj (nameOp,
    {
      {"op", UniValueType (UniValue::VSTR)},
      {"name", UniValueType (UniValue::VSTR)},
      {"value", UniValueType (UniValue::VSTR)},
    }
  );
  const std::string op = find_value (nameOp, "op").get_str ();
  const valtype name
    = DecodeNameFromRPCOrThrow (find_value (nameOp, "name"), NO_OPTIONS);
  const valtype value
    = DecodeValueFromRPCOrThrow (find_value (nameOp, "value"), NO_OPTIONS);

  UniValue result(UniValue::VOBJ);

  if (op == "name_register")
    mtx.vout[nOut].scriptPubKey
      = CNameScript::buildNameRegister (mtx.vout[nOut].scriptPubKey,
                                        name, value);
  else if (op == "name_update")
    mtx.vout[nOut].scriptPubKey
      = CNameScript::buildNameUpdate (mtx.vout[nOut].scriptPubKey,
                                      name, value);
  else
    throw JSONRPCError (RPC_INVALID_PARAMETER, "Invalid name operation");

  result.pushKV ("hex", EncodeHexTx (CTransaction (mtx)));
  return result;
}

/* ************************************************************************** */

UniValue
name_checkdb (const JSONRPCRequest& request)
{
  RPCHelpMan ("name_checkdb",
      "\nValidates the name DB's consistency.\n"
      "\nRoughly between blocks 139,000 and 180,000, this call is expected to fail due to the historic 'name stealing' bug.\n",
      {},
      RPCResult {
        "xxxxx                        (boolean) whether the state is valid\n"
      },
      RPCExamples {
          HelpExampleCli ("name_checkdb", "")
        + HelpExampleRpc ("name_checkdb", "")
      }
  ).Check (request);

  LOCK (cs_main);
  auto& coinsTip = ::ChainstateActive ().CoinsTip ();
  coinsTip.Flush ();
  return coinsTip.ValidateNameDB ();
}

} // namespace
/* ************************************************************************** */

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "names",              "name_show",              &name_show,              {"name","options"} },
    { "names",              "name_history",           &name_history,           {"name","options"} },
    { "names",              "name_scan",              &name_scan,              {"start","count","options"} },
    { "names",              "name_pending",           &name_pending,           {"name","options"} },
    { "names",              "name_checkdb",           &name_checkdb,           {} },
    { "rawtransactions",    "namerawtransaction",     &namerawtransaction,     {"hexstring","vout","nameop"} },
};

void RegisterNameRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
