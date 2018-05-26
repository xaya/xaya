// Copyright (c) 2014-2018 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chainparams.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "names/common.h"
#include "names/main.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "script/names.h"
#include "txmempool.h"
#include "utilstrencodings.h"
#include "validation.h"
#ifdef ENABLE_WALLET
# include "wallet/rpcwallet.h"
# include "wallet/wallet.h"
#endif

#include <boost/xpressive/xpressive_dynamic.hpp>

#include <memory>
#include <sstream>

#include <univalue.h>

/**
 * Utility routine to construct a "name info" object to return.  This is used
 * for name_show and also name_list.
 * @param name The name.
 * @param value The name's value.
 * @param outp The last update's outpoint.
 * @param addr The name's address script.
 * @param height The name's last update height.
 * @return A JSON object to return.
 */
UniValue
getNameInfo (const valtype& name, const valtype& value, const COutPoint& outp,
             const CScript& addr, int height)
{
  UniValue obj(UniValue::VOBJ);
  obj.pushKV ("name", ValtypeToString (name));
  obj.pushKV ("value", ValtypeToString (value));
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

  /* Calculate expiration data.  */
  const int curHeight = chainActive.Height ();
  const Consensus::Params& params = Params ().GetConsensus ();
  const int expireDepth = params.rules->NameExpirationDepth (curHeight);
  const int expireHeight = height + expireDepth;
  const int expiresIn = expireHeight - curHeight;
  const bool expired = (expiresIn <= 0);
  obj.pushKV ("height", height);
  obj.pushKV ("expires_in", expiresIn);
  obj.push_back (Pair ("expired", expired));

  return obj;
}

/**
 * Return name info object for a CNameData object.
 * @param name The name.
 * @param data The name's data.
 * @return A JSON object to return.
 */
UniValue
getNameInfo (const valtype& name, const CNameData& data)
{
  return getNameInfo (name, data.getValue (), data.getUpdateOutpoint (),
                      data.getAddress (), data.getHeight ());
}

/**
 * Return the help string description to use for name info objects.
 * @param indent Indentation at the line starts.
 * @param trailing Trailing string (e. g., comma for an array of these objects).
 * @return The description string.
 */
std::string
getNameInfoHelp (const std::string& indent, const std::string& trailing)
{
  std::ostringstream res;

  res << indent << "{" << std::endl;
  res << indent << "  \"name\": xxxxx,           "
      << "(string) the requested name" << std::endl;
  res << indent << "  \"value\": xxxxx,          "
      << "(string) the name's current value" << std::endl;
  res << indent << "  \"txid\": xxxxx,           "
      << "(string) the name's last update tx" << std::endl;
  res << indent << "  \"address\": xxxxx,        "
      << "(string) the address holding the name" << std::endl;
  res << indent << "  \"height\": xxxxx,         "
      << "(numeric) the name's last update height" << std::endl;
  res << indent << "  \"expires_in\": xxxxx,     "
      << "(numeric) expire counter for the name" << std::endl;
  res << indent << "  \"expired\": xxxxx,        "
      << "(boolean) whether the name is expired" << std::endl;
  res << indent << "}" << trailing << std::endl;

  return res.str ();
}

/* ************************************************************************** */
namespace
{

UniValue
name_show (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () != 1)
    throw std::runtime_error (
        "name_show \"name\"\n"
        "\nLook up the current data for the given name."
        "  Fails if the name doesn't exist.\n"
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to query for\n"
        "\nResult:\n"
        + getNameInfoHelp ("", "") +
        "\nExamples:\n"
        + HelpExampleCli ("name_show", "\"myname\"")
        + HelpExampleRpc ("name_show", "\"myname\"")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR});

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  const std::string nameStr = request.params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);

  CNameData data;
  {
    LOCK (cs_main);
    if (!pcoinsTip->GetName (name, data))
      {
        std::ostringstream msg;
        msg << "name not found: '" << nameStr << "'";
        throw JSONRPCError (RPC_WALLET_ERROR, msg.str ());
      }
  }

  return getNameInfo (name, data);
}

/* ************************************************************************** */

UniValue
name_history (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () != 1)
    throw std::runtime_error (
        "name_history \"name\"\n"
        "\nLook up the current and all past data for the given name."
        "  -namehistory must be enabled.\n"
        "\nArguments:\n"
        "1. \"name\"          (string, required) the name to query for\n"
        "\nResult:\n"
        "[\n"
        + getNameInfoHelp ("  ", ",") +
        "  ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_history", "\"myname\"")
        + HelpExampleRpc ("name_history", "\"myname\"")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR});

  if (!fNameHistory)
    throw std::runtime_error ("-namehistory is not enabled");

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  const std::string nameStr = request.params[0].get_str ();
  const valtype name = ValtypeFromString (nameStr);

  CNameData data;
  CNameHistory history;

  {
    LOCK (cs_main);

    if (!pcoinsTip->GetName (name, data))
      {
        std::ostringstream msg;
        msg << "name not found: '" << nameStr << "'";
        throw JSONRPCError (RPC_WALLET_ERROR, msg.str ());
      }

    if (!pcoinsTip->GetNameHistory (name, history))
      assert (history.empty ());
  }

  UniValue res(UniValue::VARR);
  for (const auto& entry : history.getData ())
    res.push_back (getNameInfo (name, entry));
  res.push_back (getNameInfo (name, data));

  return res;
}

/* ************************************************************************** */

UniValue
name_scan (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () > 2)
    throw std::runtime_error (
        "name_scan (\"start\" (\"count\"))\n"
        "\nList names in the database.\n"
        "\nArguments:\n"
        "1. \"start\"       (string, optional) skip initially to this name\n"
        "2. \"count\"       (numeric, optional, default=500) stop after this many names\n"
        "\nResult:\n"
        "[\n"
        + getNameInfoHelp ("  ", ",") +
        "  ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_scan", "")
        + HelpExampleCli ("name_scan", "\"d/abc\"")
        + HelpExampleCli ("name_scan", "\"d/abc\" 10")
        + HelpExampleRpc ("name_scan", "\"d/abc\"")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR, UniValue::VNUM});

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  valtype start;
  if (request.params.size () >= 1)
    start = ValtypeFromString (request.params[0].get_str ());

  int count = 500;
  if (request.params.size () >= 2)
    count = request.params[1].get_int ();

  UniValue res(UniValue::VARR);
  if (count <= 0)
    return res;

  LOCK (cs_main);

  valtype name;
  CNameData data;
  std::unique_ptr<CNameIterator> iter(pcoinsTip->IterateNames ());
  for (iter->seek (start); count > 0 && iter->next (name, data); --count)
    res.push_back (getNameInfo (name, data));

  return res;
}

/* ************************************************************************** */

UniValue
name_filter (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () > 5)
    throw std::runtime_error (
        "name_filter (\"regexp\" (\"maxage\" (\"from\" (\"nb\" (\"stat\")))))\n"
        "\nScan and list names matching a regular expression.\n"
        "\nArguments:\n"
        "1. \"regexp\"      (string, optional) filter names with this regexp\n"
        "2. \"maxage\"      (numeric, optional, default=36000) only consider names updated in the last \"maxage\" blocks; 0 means all names\n"
        "3. \"from\"        (numeric, optional, default=0) return from this position onward; index starts at 0\n"
        "4. \"nb\"          (numeric, optional, default=0) return only \"nb\" entries; 0 means all\n"
        "5. \"stat\"        (string, optional) if set to the string \"stat\", print statistics instead of returning the names\n"
        "\nResult:\n"
        "[\n"
        + getNameInfoHelp ("  ", ",") +
        "  ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("name_filter", "\"\" 5")
        + HelpExampleCli ("name_filter", "\"^id/\"")
        + HelpExampleCli ("name_filter", "\"^id/\" 36000 0 0 \"stat\"")
        + HelpExampleRpc ("name_scan", "\"^d/\"")
      );

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VNUM, UniValue::VNUM, UniValue::VNUM,
                 UniValue::VSTR});

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  /* ********************** */
  /* Interpret parameters.  */

  bool haveRegexp(false);
  boost::xpressive::sregex regexp;

  int maxage(36000), from(0), nb(0);
  bool stats(false);

  if (request.params.size () >= 1)
    {
      haveRegexp = true;
      regexp = boost::xpressive::sregex::compile (request.params[0].get_str ());
    }

  if (request.params.size () >= 2)
    maxage = request.params[1].get_int ();
  if (maxage < 0)
    throw JSONRPCError (RPC_INVALID_PARAMETER,
                        "'maxage' should be non-negative");
  if (request.params.size () >= 3)
    from = request.params[2].get_int ();
  if (from < 0)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "'from' should be non-negative");

  if (request.params.size () >= 4)
    nb = request.params[3].get_int ();
  if (nb < 0)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "'nb' should be non-negative");

  if (request.params.size () >= 5)
    {
      if (request.params[4].get_str () != "stat")
        throw JSONRPCError (RPC_INVALID_PARAMETER,
                            "fifth argument must be the literal string 'stat'");
      stats = true;
    }

  /* ******************************************* */
  /* Iterate over names to build up the result.  */

  UniValue names(UniValue::VARR);
  unsigned count(0);

  LOCK (cs_main);

  valtype name;
  CNameData data;
  std::unique_ptr<CNameIterator> iter(pcoinsTip->IterateNames ());
  while (iter->next (name, data))
    {
      const int age = chainActive.Height () - data.getHeight ();
      assert (age >= 0);
      if (maxage != 0 && age >= maxage)
        continue;

      if (haveRegexp)
        {
          const std::string nameStr = ValtypeToString (name);
          boost::xpressive::smatch matches;
          if (!boost::xpressive::regex_search (nameStr, matches, regexp))
            continue;
        }

      if (from > 0)
        {
          --from;
          continue;
        }
      assert (from == 0);

      if (stats)
        ++count;
      else
        names.push_back (getNameInfo (name, data));

      if (nb > 0)
        {
          --nb;
          if (nb == 0)
            break;
        }
    }

  /* ********************************************************** */
  /* Return the correct result (take stats mode into account).  */

  if (stats)
    {
      UniValue res(UniValue::VOBJ);
      res.pushKV ("blocks", chainActive.Height ());
      res.pushKV ("count", static_cast<int> (count));

      return res;
    }

  return names;
}

/* ************************************************************************** */

UniValue
name_pending (const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size () > 1)
    throw std::runtime_error (
        "name_pending (\"name\")\n"
        "\nList unconfirmed name operations in the mempool.\n"
        "\nIf a name is given, only check for operations on this name.\n"
        "\nArguments:\n"
        "1. \"name\"        (string, optional) only look for this name\n"
        "\nResult:\n"
        "[\n"
        "  {\n"
        "    \"op\": xxxx       (string) the operation being performed\n"
        "    \"name\": xxxx     (string) the name operated on\n"
        "    \"value\": xxxx    (string) the name's new value\n"
        "    \"txid\": xxxx     (string) the txid corresponding to the operation\n"
        "    \"ismine\": xxxx   (boolean) whether the name is owned by the wallet\n"
        "  },\n"
        "  ...\n"
        "]\n"
        + HelpExampleCli ("name_pending", "")
        + HelpExampleCli ("name_pending", "\"d/domob\"")
        + HelpExampleRpc ("name_pending", "")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR});

#ifdef ENABLE_WALLET
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest (request);
  CWallet* const pwallet = wallet.get ();
  LOCK2 (pwallet ? &pwallet->cs_wallet : nullptr, mempool.cs);
#else
  LOCK (mempool.cs);
#endif

  std::vector<uint256> txHashes;
  if (request.params.size () == 0)
    mempool.queryHashes (txHashes);
  else
    {
      const std::string name = request.params[0].get_str ();
      const valtype vchName = ValtypeFromString (name);
      const uint256 txid = mempool.getTxForName (vchName);
      if (!txid.IsNull ())
        txHashes.push_back (txid);
    }

  UniValue arr(UniValue::VARR);
  for (std::vector<uint256>::const_iterator i = txHashes.begin ();
       i != txHashes.end (); ++i)
    {
      std::shared_ptr<const CTransaction> tx = mempool.get (*i);
      if (!tx || !tx->IsNamecoin ())
        continue;

      for (const auto& txOut : tx->vout)
        {
          const CNameScript op(txOut.scriptPubKey);
          if (!op.isNameOp () || !op.isAnyUpdate ())
            continue;

          const valtype vchName = op.getOpName ();
          const valtype vchValue = op.getOpValue ();

          const std::string name = ValtypeToString (vchName);
          const std::string value = ValtypeToString (vchValue);

          std::string strOp;
          switch (op.getNameOp ())
            {
            case OP_NAME_FIRSTUPDATE:
              strOp = "name_firstupdate";
              break;
            case OP_NAME_UPDATE:
              strOp = "name_update";
              break;
            default:
              assert (false);
            }

          UniValue obj(UniValue::VOBJ);
          obj.pushKV ("op", strOp);
          obj.pushKV ("name", name);
          obj.pushKV ("value", value);
          obj.pushKV ("txid", tx->GetHash ().GetHex ());

#ifdef ENABLE_WALLET
          isminetype mine = ISMINE_NO;
          if (pwallet)
            mine = IsMine (*pwallet, op.getAddress ());
          const bool isMine = (mine & ISMINE_SPENDABLE);
          obj.push_back (Pair ("ismine", isMine));
#endif

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
        "namerawtransaction \"hexstring\" vout nameop\n"
        "\nAdd a name operation to an existing raw transaction.\n"
        "\nUse createrawtransaction first to create the basic transaction,\n"
        "including the required inputs and outputs also for the name.\n"
        "\nArguments:\n"
        "1. \"hexstring\"       (string, required) The transaction hex string\n"
        "2. vout              (numeric, required) The vout of the desired name output\n"
        "3. nameop            (object, required) Json object for name operation.\n"
        "                     The operation can be either of:\n"
        "    {\n"
        "      \"op\": \"name_new\",\n"
        "      \"name\": xxx,         (string, required) The name to register\n"
        "      \"rand\": xxx,         (string, optional) The nonce value to use\n"
        "    }\n"
        "    {\n"
        "      \"op\": \"name_firstupdate\",\n"
        "      \"name\": xxx,         (string, required) The name to register\n"
        "      \"value\": xxx,        (string, required) The name's value\n"
        "      \"rand\": xxx,         (string, required) The nonce used in name_new\n"
        "    }\n"
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
        + HelpExampleCli ("namerawtransaction", R"("raw tx hex" 1 "{\"op\":\"name_new\",\"name\":\"my-name\")")
        + HelpExampleCli ("namerawtransaction", R"("raw tx hex" 1 "{\"op\":\"name_firstupdate\",\"name\":\"my-name\",\"value\":\"new value\",\"rand\":\"00112233\")")
        + HelpExampleCli ("namerawtransaction", R"("raw tx hex" 1 "{\"op\":\"name_update\",\"name\":\"my-name\",\"value\":\"new value\")")
        + HelpExampleRpc ("namerawtransaction", R"("raw tx hex", 1, "{\"op\":\"name_update\",\"name\":\"my-name\",\"value\":\"new value\")")
      );

  RPCTypeCheck (request.params,
                {UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ});

  CMutableTransaction mtx;
  if (!DecodeHexTx (mtx, request.params[0].get_str (), true))
    throw JSONRPCError (RPC_DESERIALIZATION_ERROR, "TX decode failed");
  mtx.SetNamecoin ();

  const size_t nOut = request.params[1].get_int ();
  if (nOut >= mtx.vout.size ())
    throw JSONRPCError (RPC_INVALID_PARAMETER, "vout is out of range");

  const UniValue nameOp = request.params[2].get_obj ();
  RPCTypeCheckObj (nameOp,
    {
      {"op", UniValueType (UniValue::VSTR)},
    }
  );
  const std::string op = find_value (nameOp, "op").get_str ();

  UniValue result(UniValue::VOBJ);

  if (op == "name_new")
    {
      RPCTypeCheckObj (nameOp,
        {
          {"name", UniValueType (UniValue::VSTR)},
          {"rand", UniValueType (UniValue::VSTR)},
        },
        true);

      valtype rand;
      if (nameOp.exists ("rand"))
        {
          const std::string randStr = find_value (nameOp, "rand").get_str ();
          if (!IsHex (randStr))
            throw JSONRPCError (RPC_DESERIALIZATION_ERROR, "rand must be hex");
          rand = ParseHex (randStr);
        }
      else
        {
          rand.resize (20);
          GetRandBytes (&rand[0], rand.size ());
        }

      const valtype name
        = ValtypeFromString (find_value (nameOp, "name").get_str ());

      valtype toHash(rand);
      toHash.insert (toHash.end (), name.begin (), name.end ());
      const uint160 hash = Hash160 (toHash);

      mtx.vout[nOut].scriptPubKey
        = CNameScript::buildNameNew (mtx.vout[nOut].scriptPubKey, hash);
      result.pushKV ("rand", HexStr (rand.begin (), rand.end ()));
    }
  else if (op == "name_firstupdate")
    {
      RPCTypeCheckObj (nameOp,
        {
          {"name", UniValueType (UniValue::VSTR)},
          {"value", UniValueType (UniValue::VSTR)},
          {"rand", UniValueType (UniValue::VSTR)},
        }
      );

      const std::string randStr = find_value (nameOp, "rand").get_str ();
      if (!IsHex (randStr))
        throw JSONRPCError (RPC_DESERIALIZATION_ERROR, "rand must be hex");
      const valtype rand = ParseHex (randStr);

      const valtype name
        = ValtypeFromString (find_value (nameOp, "name").get_str ());
      const valtype value
        = ValtypeFromString (find_value (nameOp, "value").get_str ());

      mtx.vout[nOut].scriptPubKey
        = CNameScript::buildNameFirstupdate (mtx.vout[nOut].scriptPubKey,
                                             name, value, rand);
    }
  else if (op == "name_update")
    {
      RPCTypeCheckObj (nameOp,
        {
          {"name", UniValueType (UniValue::VSTR)},
          {"value", UniValueType (UniValue::VSTR)},
        }
      );

      const valtype name
        = ValtypeFromString (find_value (nameOp, "name").get_str ());
      const valtype value
        = ValtypeFromString (find_value (nameOp, "value").get_str ());

      mtx.vout[nOut].scriptPubKey
        = CNameScript::buildNameUpdate (mtx.vout[nOut].scriptPubKey,
                                        name, value);
    }
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
        "name_checkdb\n"
        "\nValidate the name DB's consistency.\n"
        "\nRoughly between blocks 139,000 and 180,000, this call is expected\n"
        "to fail due to the historic 'name stealing' bug.\n"
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
    { "names",              "name_show",              &name_show,              {"name"} },
    { "names",              "name_history",           &name_history,           {"name"} },
    { "names",              "name_scan",              &name_scan,              {"start","count"} },
    { "names",              "name_filter",            &name_filter,            {"regexp","maxage","from","nb","stat"} },
    { "names",              "name_pending",           &name_pending,           {"name"} },
    { "names",              "name_checkdb",           &name_checkdb,           {} },
    { "rawtransactions",    "namerawtransaction",     &namerawtransaction,     {"hexstring","vout","nameop"} },
};

void RegisterNameRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
