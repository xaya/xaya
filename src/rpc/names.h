// Copyright (c) 2014-2018 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_NAMES_H
#define BITCOIN_RPC_NAMES_H

#include <script/script.h>

#include <sstream>
#include <string>

class CNameData;
class COutPoint;
class CScript;
class UniValue;

UniValue getNameInfo (const valtype& name, const valtype& value,
                      const COutPoint& outp, const CScript& addr);
UniValue getNameInfo (const valtype& name, const CNameData& data);
void addExpirationInfo (int height, UniValue& data);

#ifdef ENABLE_WALLET
class CWallet;
void addOwnershipInfo (const CScript& addr,
                       const CWallet* pwallet,
                       UniValue& data);
#endif

/**
 * Builder class for the help text of RPCs that return information about
 * names (like name_show, name_scan, name_pending or name_list).  Since the
 * exact fields contained and formatting to use depend on the case, this class
 * provides a simple and fluent interface to build the right help text for
 * each case.
 */
class NameInfoHelp
{

private:

  std::ostringstream result;
  const std::string indent;

public:

  explicit NameInfoHelp (const std::string& ind);

  NameInfoHelp& withField (const std::string& field, const std::string& doc);
  NameInfoHelp& withExpiration ();

  std::string finish (const std::string& trailing);

};

#endif // BITCOIN_RPC_NAMES_H
