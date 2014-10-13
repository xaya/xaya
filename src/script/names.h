// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_SCRIPT_NAMES
#define H_BITCOIN_SCRIPT_NAMES

#include "script/script.h"

/**
 * A script parsed for name operations.  This can be initialised
 * from a "standard" script, and will then determine if this is
 * a name operation and which parts it consists of.
 */
class CNameScript
{

private:

  /** The type of operation.  OP_NOP if no (valid) name op.  */
  opcodetype op;

  /** The non-name part, i. e., the address.  */
  CScript address;

  /** The operation arguments.  */
  std::vector<valtype> args;

public:

  /**
   * Parse a script and determine whether it is a valid name script.  Sets
   * the member variables representing the "picked apart" name script.
   * @param script The ordinary script to parse.
   */
  explicit CNameScript (const CScript& script);

  /**
   * Return whether this is a (valid) name script.
   * @return True iff this is a name operation.
   */
  inline bool
  isNameOp () const
  {
    switch (op)
      {
      case OP_NAME_NEW:
      case OP_NAME_FIRSTUPDATE:
      case OP_NAME_UPDATE:
        return true;

      case OP_NOP:
        return false;

      default:
        assert (false);
      }
  }

  /**
   * Return the non-name script.
   * @return The address part.
   */
  inline const CScript&
  getAddress () const
  {
    return address;
  }

};

#endif // H_BITCOIN_SCRIPT_NAMES
