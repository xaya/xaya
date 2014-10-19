// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES
#define H_BITCOIN_NAMES

#include "serialize.h"

#include "script/script.h"

#include <string>

class CBlockUndo;
class CCoinsView;
class CCoinsViewCache;
class CNameScript;
class CLevelDBBatch;
class CTransaction;
class CValidationState;

/* Some constants defining name limits.  */
static const unsigned MAX_VALUE_LENGTH = 1023;
static const unsigned MAX_NAME_LENGTH = 255;
static const unsigned MIN_FIRSTUPDATE_DEPTH = 12;

/**
 * Construct a valtype (e. g., name) from a string.
 * @param str The string input.
 * @return The corresponding valtype.
 */
inline valtype
ValtypeFromString (const std::string& str)
{
  return valtype (str.begin (), str.end ());
}

/**
 * Convert a valtype to a string.
 * @param val The valtype value.
 * @return Corresponding string.
 */
inline std::string
ValtypeToString (const valtype& val)
{
  return std::string (val.begin (), val.end ());
}

/**
 * Information stored for a name in the database.
 */
class CNameData
{

private:

  /** The name's value.  */
  valtype value;

  /** The transaction's height.  Used for expiry.  */
  unsigned nHeight;

  /**
   * The name's address (as script).  This is kept here also, because
   * that information is useful to extract on demand (e. g., in name_show).
   */
  CScript addr;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action,
                                 int nType, int nVersion)
  {
    READWRITE (value);
    READWRITE (nHeight);
    READWRITE (addr);
  }

  /* Compare for equality.  */
  friend inline bool
  operator== (const CNameData& a, const CNameData& b)
  {
    return a.value == b.value && a.nHeight == b.nHeight && a.addr == b.addr;
  }
  friend inline bool
  operator!= (const CNameData& a, const CNameData& b)
  {
    return !(a == b);
  }

  /**
   * Get the height.
   * @return The name's update height.
   */
  inline unsigned
  getHeight () const
  {
    return nHeight;
  }

  /**
   * Get the value.
   * @return The name's value.
   */
  inline const valtype&
  getValue () const
  {
    return value;
  }

  /**
   * Get the address.
   * @return The name's address.
   */
  inline const CScript&
  getAddress () const
  {
    return addr;
  }

  /**
   * Set from a name update operation.
   * @param h The height (not available from script).
   * @param script The name script.  Should be a name (first) update.
   */
  void fromScript (unsigned h, const CNameScript& script);

};

/**
 * Cache / record of updates to the name database.  In addition to
 * new names (or updates to them), this also keeps track of deleted names
 * (when rolling back changes).
 */
class CNameCache
{

private:

  /** New or updated names.  */
  std::map<valtype, CNameData> entries;
  /** Deleted names.  */
  std::set<valtype> deleted;

public:

  CNameCache ()
    : entries(), deleted()
  {}

  inline void
  clear ()
  {
    entries.clear ();
    deleted.clear ();
  }

  /* See if the given name is marked as deleted.  */
  inline bool
  isDeleted (const valtype& name) const
  {
    return (deleted.count (name) > 0); 
  }

  /* Try to get a name's associated data.  This looks only
     in entries, and doesn't care about deleted data.  */
  bool get (const valtype& name, CNameData& data) const;

  /* Insert (or update) a name.  If it is marked as "deleted", this also
     removes the "deleted" mark.  */
  void set (const valtype& name, const CNameData& data);

  /* Delete a name.  If it is in the "entries" set also, remove it there.  */
  void remove (const valtype& name);

  /* Apply all the changes in the passed-in record on top of this one.  */
  void apply (const CNameCache& cache);

  /* Write all cached changes to a database batch update object.  */
  void writeBatch (CLevelDBBatch& batch) const;

};

/**
 * Undo information for one name operation.  This contains either the
 * information that the name was newly created (and should thus be
 * deleted entirely) or that it was updated including the old value.
 */
class CNameTxUndo
{

private:

  /** The name this concerns.  */
  valtype name;

  /** Whether this was an entirely new name (no update).  */
  bool isNew;

  /** The old name value that was overwritten by the operation.  */
  CNameData oldData;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action,
                                 int nType, int nVersion)
  {
    READWRITE (name);
    READWRITE (isNew);
    if (!isNew)
      READWRITE (oldData);
  }

  /**
   * Set the data for an update/registration of the given name.  The CCoinsView
   * is used to find out all the necessary information.
   * @param nm The name that is being updated.
   * @param view The (old!) chain state.
   */
  void fromOldState (const valtype& nm, const CCoinsView& view);

  /**
   * Apply the undo to the chain state given.
   * @param view The chain state to update ("undo").
   */
  void apply (CCoinsViewCache& view) const;

};

/**
 * Check a transaction according to the additional Namecoin rules.  This
 * ensures that all name operations (if any) are valid and that it has
 * name operations iff it is marked as Namecoin tx by its version.
 * @param tx The transaction to check.
 * @param nHeight Height at which the tx will be.  May be MEMPOOL_HEIGHT.
 * @param view The current chain state.
 * @param state Resulting validation state.
 * @return True in case of success.
 */
bool CheckNameTransaction (const CTransaction& tx, unsigned nHeight,
                           const CCoinsView& view,
                           CValidationState& state);

/**
 * Apply the changes of a name transaction to the name database.
 * @param tx The transaction to apply.
 * @param nHeight Height at which the tx is.  Used for CNameData.
 * @param view The chain state to update.
 * @param undo Record undo information here.
 * @return True in case of success.
 */
void ApplyNameTransaction (const CTransaction& tx, unsigned nHeight,
                           CCoinsViewCache& view, CBlockUndo& undo);

#endif // H_BITCOIN_NAMES
