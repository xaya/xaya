// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES_COMMON
#define H_BITCOIN_NAMES_COMMON

#include "primitives/transaction.h"
#include "script/script.h"
#include "serialize.h"

#include <map>
#include <set>

class CNameScript;
class CLevelDBBatch;

/** Whether or not name history is enabled.  */
extern bool fNameHistory;

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

/* ************************************************************************** */
/* CNameData.  */

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

  /** The name's last update outpoint.  */
  COutPoint prevout;

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
    READWRITE (prevout);
    READWRITE (addr);
  }

  /* Compare for equality.  */
  friend inline bool
  operator== (const CNameData& a, const CNameData& b)
  {
    return a.value == b.value && a.nHeight == b.nHeight
            && a.prevout == b.prevout && a.addr == b.addr;
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
   * Get the name's update outpoint.
   * @return The update outpoint.
   */
  inline const COutPoint&
  getUpdateOutpoint () const
  {
    return prevout;
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
   * Check if the name is expired at the current chain height.
   * @return True iff the name is expired.
   */
  bool isExpired () const;

  /**
   * Check if the name is expired at the given height.
   * @param h The height at which to check.
   * @return True iff the name is expired at height h.
   */
  bool isExpired (unsigned h) const;

  /**
   * Set from a name update operation.
   * @param h The height (not available from script).
   * @param out The update outpoint.
   * @param script The name script.  Should be a name (first) update.
   */
  void fromScript (unsigned h, const COutPoint& out, const CNameScript& script);

};

/* ************************************************************************** */
/* CNameHistory.  */

/**
 * Keep track of a name's history.  This is a stack of old CNameData
 * objects that have been obsoleted.
 */
class CNameHistory
{

private:

  /** The actual data.  */
  std::vector<CNameData> data;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action,
                                 int nType, int nVersion)
  {
    READWRITE (data);
  }

  /**
   * Check if the stack is empty.  This is used to decide when to fully
   * delete an entry in the database.
   * @return True iff the data stack is empty.
   */
  inline bool
  empty () const
  {
    return data.empty ();
  }

  /**
   * Access the data in a read-only way.
   * @return The data stack.
   */
  inline const std::vector<CNameData>&
  getData () const
  {
    return data;
  }

  /**
   * Push a new entry onto the data stack.  The new entry's height should
   * be at least as high as the stack top entry's.  If not, fail.
   * @param entry The new entry to push onto the stack.
   */
  inline void
  push (const CNameData& entry)
  {
    assert (data.empty () || data.back ().getHeight () <= entry.getHeight ());
    data.push_back (entry);
  }

  /**
   * Pop the top entry off the stack.  This is used when undoing name
   * changes.  The name's new value is passed as argument and should
   * match the removed entry.  If not, fail.
   * @param entry The name's value after undoing.
   */
  inline void
  pop (const CNameData& entry)
  {
    assert (!data.empty () && data.back () == entry);
    data.pop_back ();
  }

};

/* ************************************************************************** */
/* CNameWalker.  */

/**
 * Interface for objects that "consume" name database entries while iterating
 * over the database.  This is used by name_scan and name_filter.
 */
class CNameWalker
{

public:

  /**
   * Called for each name.
   * @param name The name.
   * @param data The name's data.
   * @return True if the iteration should continue.
   */
  virtual bool nextName (const valtype& name, const CNameData& data) = 0;

};

/* ************************************************************************** */
/* CNameCache.  */

/**
 * Cache / record of updates to the name database.  In addition to
 * new names (or updates to them), this also keeps track of deleted names
 * (when rolling back changes).
 */
class CNameCache
{

public:

  /**
   * Type for expire-index entries.  We have to make sure that
   * it is serialised in such a way that ordering is done correctly
   * by height.  This is not true if we use a std::pair, since then
   * the height is serialised as byte-array with little-endian order,
   * which does not correspond to the ordering by actual value.
   */
  class ExpireEntry
  {
  public:

    unsigned nHeight;
    valtype name;

    inline ExpireEntry ()
      : nHeight(0), name()
    {}

    inline ExpireEntry (unsigned h, const valtype& n)
      : nHeight(h), name(n)
    {}

    /* Default copy and assignment.  */

    inline size_t
    GetSerializeSize (int nType, int nVersion) const
    {
      return sizeof (nHeight) + ::GetSerializeSize (name, nType, nVersion);
    }

    template<typename Stream>
      inline void
      Serialize (Stream& s, int nType, int nVersion) const
    {
      /* Flip the byte order of nHeight to big endian.  */
      unsigned nHeightFlipped = nHeight;
      char* heightPtr = reinterpret_cast<char*> (&nHeightFlipped);
      std::reverse (heightPtr, heightPtr + sizeof (nHeightFlipped));

      WRITEDATA (s, nHeightFlipped);
      ::Serialize (s, name, nType, nVersion);
    }

    template<typename Stream>
      inline void
      Unserialize (Stream& s, int nType, int nVersion)
    {
      READDATA (s, nHeight);
      ::Unserialize (s, name, nType, nVersion);

      /* Unflip the byte order.  */
      char* heightPtr = reinterpret_cast<char*> (&nHeight);
      std::reverse (heightPtr, heightPtr + sizeof (nHeight));
    }

    friend inline bool
    operator== (const ExpireEntry& a, const ExpireEntry& b)
    {
      return a.nHeight == b.nHeight && a.name == b.name;
    }

    friend inline bool
    operator!= (const ExpireEntry& a, const ExpireEntry& b)
    {
      return !(a == b);
    }

    friend inline bool
    operator< (const ExpireEntry& a, const ExpireEntry& b)
    {
      if (a.nHeight != b.nHeight)
        return a.nHeight < b.nHeight;

      return a.name < b.name;
    }

  };

private:

  /** New or updated names.  */
  std::map<valtype, CNameData> entries;
  /** Deleted names.  */
  std::set<valtype> deleted;

  /**
   * New or updated history stacks.  If they are empty, the corresponding
   * database entry is deleted instead.
   */
  std::map<valtype, CNameHistory> history;

  /**
   * Changes to be performed to the expire index.  The entry is mapped
   * to either "true" (meaning to add it) or "false" (delete).
   */
  std::map<ExpireEntry, bool> expireIndex;

public:

  inline void
  clear ()
  {
    entries.clear ();
    deleted.clear ();
    history.clear ();
    expireIndex.clear ();
  }

  /**
   * Check if the cache is "clean" (no cached changes).  This also
   * performs internal checks and fails with an assertion if the
   * internal state is inconsistent.
   * @return True iff no changes are cached.
   */
  inline bool
  empty () const
  {
    if (entries.empty () && deleted.empty ())
      {
        assert (history.empty () && expireIndex.empty ());
        return true;
      }

    return false;
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

  /**
   * Query for an history entry.
   * @param name The name to look up.
   * @param res Put the resulting history entry here.
   * @return True iff the name was found in the cache.
   */
  bool getHistory (const valtype& name, CNameHistory& res) const;

  /* Query the cached changes to the expire index.  In particular,
     for a given height and a given set of names that were indexed to
     this update height, apply possible changes to the set that
     are represented by the cached expire index changes.  */
  void updateNamesForHeight (unsigned nHeight, std::set<valtype>& names) const;

  /* Insert (or update) a name.  If it is marked as "deleted", this also
     removes the "deleted" mark.  */
  void set (const valtype& name, const CNameData& data);

  /**
   * Set a name history entry.
   * @param name The name to modify.
   * @param data The new history entry.
   */
  void setHistory (const valtype& name, const CNameHistory& data);

  /* Delete a name.  If it is in the "entries" set also, remove it there.  */
  void remove (const valtype& name);

  /* Add an expire-index entry.  */
  void addExpireIndex (const valtype& name, unsigned height);

  /* Remove an expire-index entry.  */
  void removeExpireIndex (const valtype& name, unsigned height);

  /* Apply all the changes in the passed-in record on top of this one.  */
  void apply (const CNameCache& cache);

  /* Write all cached changes to a database batch update object.  */
  void writeBatch (CLevelDBBatch& batch) const;

};

#endif // H_BITCOIN_NAMES_COMMON
