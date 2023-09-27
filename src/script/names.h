// Copyright (c) 2014-2023 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_SCRIPT_NAMES
#define H_BITCOIN_SCRIPT_NAMES

#include <script/script.h>

class uint160;

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
   * Default constructor.  This enables us to declare a variable
   * and initialise it later via assignment.
   */
  inline CNameScript ()
    : op(OP_NOP)
  {}

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
      case OP_NAME_REGISTER:
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

  /**
   * Return the name operation.  This returns OP_NAME_NEW, OP_NAME_FIRSTUPDATE
   * or OP_NAME_UPDATE.  Do not call if this is not a name script.
   * @return The name operation opcode.
   */
  inline opcodetype
  getNameOp () const
  {
    switch (op)
      {
      case OP_NAME_REGISTER:
      case OP_NAME_UPDATE:
        return op;

      default:
        assert (false);
      }
  }

  /**
   * Return whether this is a name update (including first updates).  I. e.,
   * whether this creates a name index update/entry.
   * @return True iff this is NAME_FIRSTUPDATE or NAME_UPDATE.
   */
  inline bool
  isAnyUpdate () const
  {
    switch (op)
      {
      case OP_NAME_REGISTER:
      case OP_NAME_UPDATE:
        return true;

      default:
        assert (false);
      }
  }

  /**
   * Return the name operation name.  This call is only valid for
   * OP_NAME_FIRSTUPDATE or OP_NAME_UPDATE.
   * @return The name operation's name.
   */
  inline const valtype&
  getOpName () const
  {
    switch (op)
      {
      case OP_NAME_REGISTER:
      case OP_NAME_UPDATE:
        return args[0];

      default:
        assert (false);
      }
  }

  /**
   * Return the name operation value.  This call is only valid for
   * OP_NAME_FIRSTUPDATE or OP_NAME_UPDATE.
   * @return The name operation's value.
   */
  inline const valtype&
  getOpValue () const
  {
    switch (op)
      {
      case OP_NAME_REGISTER:
      case OP_NAME_UPDATE:
        return args[1];

      default:
        assert (false);
      }
  }

  /**
   * Check if the given script is a name script.  This is a utility method.
   * @param script The script to parse.
   * @return True iff it is a name script.
   */
  static inline bool
  isNameScript (const CScript& script)
  {
    const CNameScript op(script);
    return op.isNameOp ();
  }

  /**
   * Concats together a base address and name prefix script.
   */
  static CScript AddNamePrefix (const CScript& addr, const CScript& prefix);

  /**
   * Build a NAME_REGISTER transaction.
   * @param addr The address script to append.
   * @param name The name to firstupdate.
   * @param value The value to set it to.
   * @return The full NAME_REGISTER script.
   */
  static CScript buildNameRegister (const CScript& addr, const valtype& name,
                                    const valtype& value);

  /**
   * Build a NAME_UPDATE transaction.
   * @param addr The address script to append.
   * @param name The name to update.
   * @param value The value to set it to.
   * @return The full NAME_UPDATE script.
   */
  static CScript buildNameUpdate (const CScript& addr, const valtype& name,
                                  const valtype& value);

};

#endif // H_BITCOIN_SCRIPT_NAMES
