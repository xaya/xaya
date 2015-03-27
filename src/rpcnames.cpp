// Copyright (c) 2014-2015 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chainparams.h"
#include "main.h"
#include "names/common.h"
#include "names/main.h"
#include "primitives/transaction.h"
#include "rpcserver.h"
#include "script/names.h"

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include <boost/xpressive/xpressive_dynamic.hpp>

#include <sstream>

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
json_spirit::Object
getNameInfo (const valtype& name, const valtype& value, const COutPoint& outp,
             const CScript& addr, int height)
{
  json_spirit::Object obj;
  obj.push_back (json_spirit::Pair ("name", ValtypeToString (name)));
  obj.push_back (json_spirit::Pair ("value", ValtypeToString (value)));
  obj.push_back (json_spirit::Pair ("txid", outp.hash.GetHex ()));
  obj.push_back (json_spirit::Pair ("vout", static_cast<int> (outp.n)));

  /* Try to extract the address.  May fail if we can't parse the script
     as a "standard" script.  */
  CTxDestination dest;
  CBitcoinAddress addrParsed;
  std::string addrStr;
  if (ExtractDestination (addr, dest) && addrParsed.Set (dest))
    addrStr = addrParsed.ToString ();
  else
    addrStr = "<nonstandard>";
  obj.push_back (json_spirit::Pair ("address", addrStr));

  /* Calculate expiration data.  */
  const int curHeight = chainActive.Height ();
  const Consensus::Params& params = Params ().GetConsensus ();
  const int expireDepth = params.rules->NameExpirationDepth (curHeight);
  const int expireHeight = height + expireDepth;
  const int expiresIn = expireHeight - curHeight;
  const bool expired = (expiresIn <= 0);
  obj.push_back (json_spirit::Pair ("height", height));
  obj.push_back (json_spirit::Pair ("expires_in", expiresIn));
  obj.push_back (json_spirit::Pair ("expired", expired));

  return obj;
}

/**
 * Return name info object for a CNameData object.
 * @param name The name.
 * @param data The name's data.
 * @return A JSON object to return.
 */
json_spirit::Object
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

/**
 * Implement the rawtx name operation feature.  This routine interprets
 * the given JSON object describing the desired name operation and then
 * modifies the transaction accordingly.
 * @param tx The transaction to extend.
 * @param obj The name operation "description" as given to the call.
 */
void
AddRawTxNameOperation (CMutableTransaction& tx, const json_spirit::Object& obj)
{
  json_spirit::Value val = json_spirit::find_value (obj, "op");
  if (val.type () != json_spirit::str_type)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "missing op key");
  const std::string op = val.get_str ();

  if (op != "name_update")
    throw JSONRPCError (RPC_INVALID_PARAMETER,
                        "only name_update is implemented for the rawtx API");

  val = json_spirit::find_value (obj, "name");
  if (val.type () != json_spirit::str_type)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "missing name key");
  const valtype name = ValtypeFromString (val.get_str ());

  val = json_spirit::find_value (obj, "value");
  if (val.type () != json_spirit::str_type)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "missing value key");
  const valtype value = ValtypeFromString (val.get_str ());

  val = json_spirit::find_value (obj, "address");
  if (val.type () != json_spirit::str_type)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "missing address key");
  const CBitcoinAddress toAddress(val.get_str ());
  if (!toAddress.IsValid ())
    throw JSONRPCError (RPC_INVALID_ADDRESS_OR_KEY, "invalid address");
  const CScript addr = GetScriptForDestination (toAddress.Get ());

  tx.SetNamecoin ();

  /* We do not add the name input.  This has to be done explicitly,
     but is easy from the name_show output.  That way, createrawtransaction
     doesn't depend on the chainstate at all.  */

  const CScript outScript = CNameScript::buildNameUpdate (addr, name, value);
  tx.vout.push_back (CTxOut (NAME_LOCKED_AMOUNT, outScript));
}

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
        + getNameInfoHelp ("", "") +
        "\nExamples:\n"
        + HelpExampleCli ("name_show", "\"myname\"")
        + HelpExampleRpc ("name_show", "\"myname\"")
      );

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  const std::string nameStr = params[0].get_str ();
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

json_spirit::Value
name_history (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || params.size () != 1)
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

  if (!fNameHistory)
    throw std::runtime_error ("-namehistory is not enabled");

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  const std::string nameStr = params[0].get_str ();
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

  json_spirit::Array res;
  BOOST_FOREACH (const CNameData& entry, history.getData ())
    res.push_back (getNameInfo (name, entry));
  res.push_back (getNameInfo (name, data));

  return res;
}

/* ************************************************************************** */

/**
 * CNameWalker object used for name_scan.
 */
class CNameScanWalker : public CNameWalker
{

private:

  /** Build up the result array.  */
  json_spirit::Array res;

  /** Count remaining names to return.  */
  unsigned count;

public:

  /**
   * Construct the object, given the count.
   * @param c The count to return.
   */
  explicit inline CNameScanWalker (unsigned c)
    : res(), count(c)
  {}

  /**
   * Return the result array.
   * @return The result array.
   */
  inline const json_spirit::Array&
  getArray () const
  {
    return res;
  }

  /**
   * Register a new name.
   * @param name The name.
   * @param data The name's data.
   * @return True if there's still remaining count.
   */
  bool nextName (const valtype& name, const CNameData& data);

};

bool
CNameScanWalker::nextName (const valtype& name, const CNameData& data)
{
  res.push_back (getNameInfo (name, data));

  assert (count > 0);
  --count;

  return count > 0;
}

json_spirit::Value
name_scan (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || params.size () > 2)
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

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  valtype start;
  if (params.size () >= 1)
    start = ValtypeFromString (params[0].get_str ());

  int count = 500;
  if (params.size () >= 2)
    count = params[1].get_int ();

  if (count <= 0)
    return json_spirit::Array ();

  CNameScanWalker walker(count);
  {
    LOCK (cs_main);
    pcoinsTip->Flush ();
    pcoinsTip->WalkNames (start, walker);
  }

  return walker.getArray ();
}

/* ************************************************************************** */

/**
 * CNameWalker object used for name_filter.
 */
class CNameFilterWalker : public CNameWalker
{

private:

  /** Whether we have a regexp to use.  */
  bool haveRegexp;
  /** The regexp to apply.  */
  boost::xpressive::sregex regexp;

  /** Maxage of 0 if no filtering.  */
  int maxage;

  /** From index (starting at 0).  */
  int from;

  /** Number of entries to return (0 means all).  */
  int nb;

  /** Collect only statistics?  */
  bool stats;

  /** In non-stats mode, build up the result here.  */
  json_spirit::Array names;
  /** Count names in stats mode.  */
  unsigned count;

public:

  /**
   * Construct the object from the name_filter parameters.
   * @param params The parameters given to name_filter.
   */
  explicit CNameFilterWalker (const json_spirit::Array& params);

  /**
   * Return the constructed result.
   * @return The name_filter result.
   */
  json_spirit::Value getResult () const;

  /**
   * Register a new name.
   * @param name The name.
   * @param data The name's data.
   * @return True if there's still remaining count.
   */
  bool nextName (const valtype& name, const CNameData& data);

};

CNameFilterWalker::CNameFilterWalker (const json_spirit::Array& params)
  : haveRegexp(false), regexp(), maxage(36000), from(0), nb(0), stats(false),
    names(), count(0)
{
  if (params.size () >= 1)
    {
      haveRegexp = true;
      regexp = boost::xpressive::sregex::compile (params[0].get_str ());
    }

  if (params.size () >= 2)
    maxage = params[1].get_int ();
  if (maxage < 0)
    throw JSONRPCError (RPC_INVALID_PARAMETER,
                        "'maxage' should be non-negative");
  if (params.size () >= 3)
    from = params[2].get_int ();
  if (from < 0)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "'from' should be non-negative");

  if (params.size () >= 4)
    nb = params[3].get_int ();
  if (nb < 0)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "'nb' should be non-negative");

  if (params.size () >= 5)
    {
      if (params[4].get_str () != "stat")
        throw JSONRPCError (RPC_INVALID_PARAMETER,
                            "fifth argument must be the literal string 'stat'");
      stats = true;
    }
}

json_spirit::Value
CNameFilterWalker::getResult () const
{
  AssertLockHeld (cs_main);

  if (stats)
    {
      json_spirit::Object res;
      res.push_back (json_spirit::Pair ("blocks", chainActive.Height ()));
      res.push_back (json_spirit::Pair ("count", static_cast<int> (count)));

      return res;
    }

  return names;
}

bool
CNameFilterWalker::nextName (const valtype& name, const CNameData& data)
{
  AssertLockHeld (cs_main);

  const int age = chainActive.Height () - data.getHeight ();
  assert (age >= 0);
  if (maxage != 0 && age >= maxage)
    return true;

  if (haveRegexp)
    {
      const std::string nameStr = ValtypeToString (name);
      boost::xpressive::smatch matches;
      if (!boost::xpressive::regex_search (nameStr, matches, regexp))
        return true;
    }

  if (from > 0)
    {
      --from;
      return true;
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
        return false;
    }

  return true;
}

json_spirit::Value
name_filter (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || params.size () > 5)
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

  if (IsInitialBlockDownload ())
    throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                       "Namecoin is downloading blocks...");

  /* The lock must be held throughout the call, since walker.getResult()
     uses chainActive for the block height in stats mode.  */
  LOCK (cs_main);

  CNameFilterWalker walker(params);
  pcoinsTip->Flush ();
  pcoinsTip->WalkNames (valtype (), walker);

  return walker.getResult ();
}

/* ************************************************************************** */

json_spirit::Value
name_checkdb (const json_spirit::Array& params, bool fHelp)
{
  if (fHelp || params.size () != 0)
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
