// Copyright (c) 2014-2018 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_NAMES_H
#define BITCOIN_RPC_NAMES_H

#include <script/script.h>

#include <names/encoding.h>

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
 * Decodes a name/value given through the RPC interface and throws a
 * JSONRPCError if it is invalid for the requested encoding.
 */
valtype DecodeNameFromRPCOrThrow (const UniValue& val, NameEncoding enc);

/**
 * Builder class for help texts describing JSON objects that share a common
 * part between multiple RPCs but also have specialised fields per RPC.
 * This is the generic base class that provides the main implementation.
 * Subclasses are used for the help text describing information about names
 * returned by RPCs like name_show, and for the generic "options" argument
 * that many name RPCs accept.
 */
class HelpTextBuilder
{

private:

  std::ostringstream result;
  const std::string indent;

  /** The column offset at which the "doc" strings are placed.  */
  const size_t docColumn;

public:

  explicit HelpTextBuilder (const std::string& ind, size_t col);

  HelpTextBuilder () = delete;
  HelpTextBuilder (const HelpTextBuilder&) = delete;
  void operator= (const HelpTextBuilder&) = delete;

  HelpTextBuilder& withLine (const std::string& line);
  HelpTextBuilder& withField (const std::string& field, const std::string& doc);
  HelpTextBuilder& withField (const std::string& field,
                              const std::string& delim, const std::string& doc);

  std::string finish (const std::string& trailing);

};

/**
 * Builder class for the help text of RPCs that return information about
 * names (like name_show, name_scan, name_pending or name_list).  Since the
 * exact fields contained and formatting to use depend on the case, this class
 * provides a simple and fluent interface to build the right help text for
 * each case.
 */
class NameInfoHelp : public HelpTextBuilder
{

public:

  explicit NameInfoHelp (const std::string& ind);

  NameInfoHelp& withExpiration ();

};

#endif // BITCOIN_RPC_NAMES_H
