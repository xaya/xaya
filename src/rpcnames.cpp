// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chainparams.h"
#include "core.h"
#include "init.h"
#include "main.h"
#include "names.h"
#include "rpcserver.h"

/*
#ifdef ENABLE_WALLET
# include "wallet.h"
#endif
*/

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include <sstream>

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
