// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES
#define H_BITCOIN_NAMES

#include "core.h"
#include "serialize.h"

#include "script/script.h"

#include <string>

class CNameScript;
class CLevelDBBatch;

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

#endif // H_BITCOIN_NAMES
