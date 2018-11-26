// Copyright (c) 2014-2018 Daniel Kraft
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
    {
      data.pushKV ("ismine", false);
      return;
    }

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
  : HelpTextBuilder("  ", 25)
{}

NameOptionsHelp&
NameOptionsHelp::withArg (const RPCArg& arg, const std::string& doc)
{
  std::string delim;
  switch (arg.m_type)
    {
    case RPCArg::Type::STR:
    case RPCArg::Type::STR_HEX:
    case RPCArg::Type::NUM:
    case RPCArg::Type::AMOUNT:
    case RPCArg::Type::BOOL:
      delim = ",";
      break;

    case RPCArg::Type::OBJ:
    case RPCArg::Type::OBJ_USER_KEYS:
    case RPCArg::Type::ARR:
      delim = ":";
      break;
    }
  assert (!delim.empty ());

  withField ('\"' + arg.m_name + '\"', delim, doc);
  innerArgs.push_back (arg);

  return *this;
}

NameOptionsHelp&
NameOptionsHelp::withWriteOptions ()
{
  withArg (RPCArg ("destAddress", RPCArg::Type::STR, true),
           "(string) The address to send the name output to");

  withArg (RPCArg ("sendCoins", RPCArg::Type::OBJ_USER_KEYS, true),
           "(object) Addresses to which coins should be"
           " sent additionally");
  withLine ("{");
  withField ("  \"addr1\": x", "");
  withField ("  \"addr2\": y", "");
  withLine ("  ...");
  withLine ("}");

  return *this;
}

NameOptionsHelp&
NameOptionsHelp::withNameEncoding ()
{
  withArg (RPCArg ("nameEncoding", RPCArg::Type::STR, true),
           "(string) Encoding (\"ascii\", \"utf8\" or \"hex\") of the"
           " name argument");
  return *this;
}

NameOptionsHelp&
NameOptionsHelp::withValueEncoding ()
{
  withArg (RPCArg ("valueEncoding", RPCArg::Type::STR, true),
           "(string) Encoding (\"ascii\", \"utf8\" or \"hex\") of the"
           " value argument");
  return *this;
}

RPCArg
NameOptionsHelp::buildRpcArg () const
{
  return RPCArg ("options", RPCArg::Type::OBJ, innerArgs, true, "\"options\"");
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

  if (request.fHelp || request.params.size () < 1 || request.params.size () > 2)
    throw std::runtime_error (
        RPCHelpMan ("name_show",
            "\nLooks up the current data for the given name."
            "  Fails if the name doesn't exist.\n",
            {
                {"name", RPCArg::Type::STR, false},
                optHelp.buildRpcArg (),
            })
            .ToString () +
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to query for\n"
        "2. \"options\"       (object, optional)\n"
        + optHelp.finish ("") +
        "\nResult:\n"
        + NameInfoHelp ("")
            .withHeight ()
            .finish ("") +
        "\nExamples:\n"
        + HelpExampleCli ("name_show", "\"myname\"")
        + HelpExampleRpc ("name_show", "\"myname\"")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VOBJ});

  if (IsInitialBlockDownload ())
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
    if (!pcoinsTip->GetName (name, data))
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

  if (request.fHelp || request.params.size () < 1 || request.params.size () > 2)
    throw std::runtime_error (
        RPCHelpMan ("name_history",
            "\nLooks up the current and all past data for the given name."
            "  -namehistory must be enabled.\n",
            {
                {"name", RPCArg::Type::STR, false},
                optHelp.buildRpcArg (),
            })
            .ToString () +
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to query for\n"
        "2. \"options\"       (object, optional)\n"
        + optHelp.finish ("") +
        "\nResult:\n"
        "[\n"
        + NameInfoHelp ("  ")
            .withHeight ()
            .finish (",") +
        "  ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_history", "\"myname\"")
        + HelpExampleRpc ("name_history", "\"myname\"")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VOBJ});

  if (!fNameHistory)
    throw std::runtime_error ("-namehistory is not enabled");

  if (IsInitialBlockDownload ())
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

    if (!pcoinsTip->GetName (name, data))
      {
        std::ostringstream msg;
        msg << "name not found: " << EncodeNameForMessage (name);
        throw JSONRPCError (RPC_WALLET_ERROR, msg.str ());
      }

    if (!pcoinsTip->GetNameHistory (name, history))
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
      .withArg (RPCArg ("minConf", RPCArg::Type::NUM, true),
                "(numeric, default=1) Minimum number of confirmations")
      .withArg (RPCArg ("maxConf", RPCArg::Type::NUM, true),
                "(numeric) Maximum number of confirmations")
      .withArg (RPCArg ("prefix", RPCArg::Type::STR, true),
                "(string) Filter for names with the given prefix")
      .withArg (RPCArg ("regexp", RPCArg::Type::STR, true),
                "(string) Filter for names matching the regexp");

  if (request.fHelp || request.params.size () > 3)
    throw std::runtime_error (
        RPCHelpMan ("name_scan",
            "\nLists names in the database.\n",
            {
                {"start", RPCArg::Type::STR, true},
                {"count", RPCArg::Type::NUM, true},
                optHelp.buildRpcArg (),
            })
            .ToString () +
        "\nArguments:\n"
        "1. \"start\"       (string, optional) skip initially to this name\n"
        "2. \"count\"       (numeric, optional, default=500) stop after this many names\n"
        "3. \"options\"     (object, optional)\n"
        + optHelp.finish ("") +
        "\nResult:\n"
        "[\n"
        + NameInfoHelp ("  ")
            .withHeight ()
            .finish (",") +
        "  ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_scan", "")
        + HelpExampleCli ("name_scan", "\"d/abc\"")
        + HelpExampleCli ("name_scan", "\"d/abc\" 10")
        + HelpExampleRpc ("name_scan", "\"d/abc\"")
      );

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ});

  if (IsInitialBlockDownload ())
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

  const int maxHeight = chainActive.Height () - minConf + 1;
  int minHeight = -1;
  if (maxConf >= 0)
    minHeight = chainActive.Height () - maxConf + 1;

  valtype name;
  CNameData data;
  std::unique_ptr<CNameIterator> iter(pcoinsTip->IterateNames ());
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

  if (request.fHelp || request.params.size () > 2)
    throw std::runtime_error (
        RPCHelpMan ("name_pending",
            "\nLists unconfirmed name operations in the mempool.\n"
            "\nIf a name is given, only check for operations on this name.\n",
            {
                {"name", RPCArg::Type::STR, true},
                optHelp.buildRpcArg (),
            })
            .ToString () +
        "\nArguments:\n"
        "1. \"name\"          (string, optional) only look for this name\n"
        "2. \"options\"       (object, optional)\n"
        + optHelp.finish ("") +
        "\nResult:\n"
        "[\n"
        + NameInfoHelp ("  ")
            .withField ("\"op\": xxxxx",
                        "(string) the operation being performed")
            .finish (",") +
        "  ...\n"
        "]\n"
        + HelpExampleCli ("name_pending", "")
        + HelpExampleCli ("name_pending", "\"d/domob\"")
        + HelpExampleRpc ("name_pending", "")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VOBJ}, true);

  MaybeWalletForRequest wallet(request);
  LOCK2 (wallet.getLock (), mempool.cs);

  UniValue options(UniValue::VOBJ);
  if (request.params.size () >= 2)
    options = request.params[1].get_obj ();

  std::vector<uint256> txHashes;
  if (request.params.size () == 0 || request.params[0].isNull ())
    mempool.queryHashes (txHashes);
  else
    {
      const valtype name
          = DecodeNameFromRPCOrThrow (request.params[0], options);
      const uint256 txid = mempool.getTxForName (name);
      if (!txid.IsNull ())
        txHashes.push_back (txid);
    }

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
  if (request.fHelp || request.params.size () != 3)
    throw std::runtime_error (
        RPCHelpMan ("namerawtransaction",
            "\nAdds a name operation to an existing raw transaction.\n"
            "\nUse createrawtransaction first to create the basic transaction,"
            " including the required inputs and outputs also for the name.\n",
            {
                {"hexstring", RPCArg::Type::STR_HEX, false},
                {"vout", RPCArg::Type::NUM, false},
                {"nameop", RPCArg::Type::OBJ,
                    {
                        {"op", RPCArg::Type::STR, false},
                        {"name", RPCArg::Type::STR, false},
                        {"value", RPCArg::Type::STR, true},
                        {"rand", RPCArg::Type::STR, true},
                    },
                 false, "\"nameop\""},
            })
            .ToString () +
        "\nArguments:\n"
        "1. \"hexstring\"       (string, required) The transaction hex string\n"
        "2. vout              (numeric, required) The vout of the desired name output\n"
        "3. nameop            (object, required) Json object for name operation.\n"
        "                     The operation can be either of:\n"
        "    {\n"
        "      \"op\": \"name_register\",\n"
        "      \"name\": xxx,         (string, required) The name to register\n"
        "      \"value\": xxx,        (string, required) The new value\n"
        "    },\n"
        "    {\n"
        "      \"op\": \"name_update\",\n"
        "      \"name\": xxx,         (string, required) The name to update\n"
        "      \"value\": xxx,        (string, required) The new value\n"
        "    }\n"
        "\nResult:\n"
        "{\n"
        "  \"hex\": xxx,        (string) Hex string of the updated transaction\n"
        "  \"rand\": xxx,       (string) If this is a name_new, the nonce used to create it\n"
        "}\n"
        + HelpExampleCli ("namerawtransaction", R"("raw tx hex" 1 "{\"op\":\"name_register\",\"name\":\"my-name\",\"value\":\"new value\")")
        + HelpExampleCli ("namerawtransaction", R"("raw tx hex" 1 "{\"op\":\"name_update\",\"name\":\"my-name\",\"value\":\"new value\")")
        + HelpExampleRpc ("namerawtransaction", R"("raw tx hex", 1, "{\"op\":\"name_update\",\"name\":\"my-name\",\"value\":\"new value\")")
      );

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


  result.pushKV ("hex", EncodeHexTx (mtx));
  return result;
}

/* ************************************************************************** */

UniValue
name_checkdb (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () != 0)
    throw std::runtime_error (
        RPCHelpMan ("name_checkdb",
            "\nValidates the name DB's consistency.\n"
            "\nRoughly between blocks 139,000 and 180,000, this call is"
            " expected to fail due to the historic 'name stealing' bug.\n",
            {})
            .ToString () +
        "\nResult:\n"
        "xxxxx                        (boolean) whether the state is valid\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_checkdb", "")
        + HelpExampleRpc ("name_checkdb", "")
      );

  LOCK (cs_main);
  pcoinsTip->Flush ();
  return pcoinsTip->ValidateNameDB ();
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
