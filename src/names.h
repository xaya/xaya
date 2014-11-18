// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES
#define H_BITCOIN_NAMES

#include "serialize.h"
#include "uint256.h"

#include "core/transaction.h"

#include "script/script.h"

#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>

class CBlockUndo;
class CCoinsView;
class CCoinsViewCache;
class CNameScript;
class CLevelDBBatch;
class CTxMemPool;
class CTxMemPoolEntry;
class CValidationState;

/* Some constants defining name limits.  */
static const unsigned MAX_VALUE_LENGTH = 1023;
static const unsigned MAX_NAME_LENGTH = 255;
static const unsigned MIN_FIRSTUPDATE_DEPTH = 12;
static const unsigned MAX_VALUE_LENGTH_UI = 520;

/**
 * Amount to lock in name transactions.  This is not (yet) enforced by the
 * protocol, but for acceptance to the mempool.
 */
static const CAmount NAME_LOCKED_AMOUNT = COIN / 100; 

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

/* ************************************************************************** */
/* CNameTxUndo.  */

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

/* ************************************************************************** */
/* CNameMemPool.  */

/**
 * Handle the name component of the transaction mempool.  This keeps track
 * of name operations that are in the mempool and ensures that all transactions
 * kept are consistent.  E. g., no two transactions are allowed to register
 * the same name, and name registration transactions are removed if a
 * conflicting registration makes it into a block.
 */
class CNameMemPool
{

private:

  /** The parent mempool object.  Used to, e. g., remove conflicting tx.  */
  CTxMemPool& pool;

  /**
   * Keep track of names that are registered by transactions in the pool.
   * Map name to registering transaction.
   */
  std::map<valtype, uint256> mapNameRegs;

  /** Map pending name updates to transaction IDs.  */
  std::map<valtype, uint256> mapNameUpdates;

  /**
   * Map NAME_NEW hashes to the corresponding transaction IDs.  This is
   * data that is kept only in memory but never cleared (until a restart).
   * It is used to prevent "name_new stealing", at least in a "soft" way.
   */
  std::map<valtype, uint256> mapNameNews;

public:

  /**
   * Construct with reference to parent mempool.
   * @param p The parent pool.
   */
  explicit inline CNameMemPool (CTxMemPool& p)
    : pool(p), mapNameRegs(), mapNameUpdates(), mapNameNews()
  {}

  /**
   * Check whether a particular name is being registered by
   * some transaction in the mempool.  Does not lock, this is
   * done by the parent mempool (which calls through afterwards).
   * @param name The name to check for.
   * @return True iff there's a matching name registration in the pool.
   */
  inline bool
  registersName (const valtype& name) const
  {
    return mapNameRegs.count (name) > 0;
  }

  /**
   * Check whether a particular name has a pending update.  Does not lock.
   * @param name The name to check for.
   * @return True iff there's a matching name update in the pool.
   */
  inline bool
  updatesName (const valtype& name) const
  {
    return mapNameUpdates.count (name) > 0;
  }

  /**
   * Clear all data.
   */
  inline void
  clear ()
  {
    mapNameRegs.clear ();
    mapNameUpdates.clear ();
    mapNameNews.clear ();
  }

  /**
   * Add an entry without checking it.  It should have been checked
   * already.  If this conflicts with the mempool, it may throw.
   * @param hash The tx hash.
   * @param entry The new mempool entry.
   */
  void addUnchecked (const uint256& hash, const CTxMemPoolEntry& entry);

  /**
   * Remove the given mempool entry.  It is assumed that it is present.
   * @param entry The entry to remove.
   */
  void remove (const CTxMemPoolEntry& entry);

  /**
   * Remove conflicts for the given tx, based on name operations.  I. e.,
   * if the tx registers a name that conflicts with another registration
   * in the mempool, detect this and remove the mempool tx accordingly.
   * @param tx The transaction for which we look for conflicts.
   * @param removed Put removed tx here.
   */
  void removeConflicts (const CTransaction& tx,
                        std::list<CTransaction>& removed);

  /**
   * Remove conflicts in the mempool due to unexpired names.  This removes
   * conflicting name registrations that are no longer possible.
   * @param unexpired The set of unexpired names.
   * @param removed Put removed tx here.
   */
  void removeUnexpireConflicts (const std::set<valtype>& unexpired,
                                std::list<CTransaction>& removed);
  /**
   * Remove conflicts in the mempool due to expired names.  This removes
   * conflicting name updates that are no longer possible.
   * @param expired The set of expired names.
   * @param removed Put removed tx here.
   */
  void removeExpireConflicts (const std::set<valtype>& expired,
                              std::list<CTransaction>& removed);

  /**
   * Perform sanity checks.  Throws if it fails.
   * @param coins The coins view this represents.
   */
  void check (const CCoinsView& coins) const;

  /**
   * Check if a tx can be added (based on name criteria) without
   * causing a conflict.
   * @param tx The transaction to check.
   * @return True if it doesn't conflict.
   */
  bool checkTx (const CTransaction& tx) const;

};

/* ************************************************************************** */

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
 */
void ApplyNameTransaction (const CTransaction& tx, unsigned nHeight,
                           CCoinsViewCache& view, CBlockUndo& undo);

/**
 * Expire all names at the given height.  This removes their coins
 * from the UTXO set.
 * @param height The new block height.
 * @param view The coins view to update.
 * @param undo The block undo object to record undo information.
 * @param names List all expired names here.
 * @return True if successful.
 */
bool ExpireNames (unsigned nHeight, CCoinsViewCache& view, CBlockUndo& undo,
                  std::set<valtype>& names);

/**
 * Undo name coin expirations.  This also does some checks verifying
 * that all is fine.
 * @param nHeight The height at which the names were expired.
 * @param undo The block undo object to use.
 * @param view The coins view to update.
 * @param names List all unexpired names here.
 * @return True if successful.
 */
bool UnexpireNames (unsigned nHeight, const CBlockUndo& undo,
                    CCoinsViewCache& view, std::set<valtype>& names);

/**
 * Check the name database consistency.  This calls CCoinsView::ValidateNameDB,
 * but only if applicable depending on the -checknamedb setting.  If it fails,
 * this throws an assertion failure.
 * @param disconnect Whether we are disconnecting blocks.
 */
void CheckNameDB (bool disconnect);

#endif // H_BITCOIN_NAMES
