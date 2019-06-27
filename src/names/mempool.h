// Copyright (c) 2014-2019 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES_MEMPOOL
#define H_BITCOIN_NAMES_MEMPOOL

#include <names/common.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <map>
#include <memory>
#include <set>

class CCoinsView;
class CTxMemPool;
class CTxMemPoolEntry;

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

  /** Type used for internal indices.  */
  typedef std::map<valtype, uint256> NameTxMap;

  /**
   * Keep track of names that are registered by transactions in the pool.
   * Map name to registering transaction.
   */
  NameTxMap mapNameRegs;

  /** Map pending name updates to transaction IDs.  */
  NameTxMap mapNameUpdates;

public:

  /**
   * Construct with reference to parent mempool.
   * @param p The parent pool.
   */
  explicit inline CNameMemPool (CTxMemPool& p)
    : pool(p), mapNameRegs(), mapNameUpdates()
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
   * Return txid of transaction registering or updating a name.  The returned
   * txid is null if no such tx exists.
   * @param name The name to check for.
   * @return The txid that registers/updates it.  Null if none.
   */
  uint256 getTxForName (const valtype& name) const;

  /**
   * Clear all data.
   */
  inline void
  clear ()
  {
    mapNameRegs.clear ();
    mapNameUpdates.clear ();
  }

  /**
   * Add an entry without checking it.  It should have been checked
   * already.  If this conflicts with the mempool, it may throw.
   */
  void addUnchecked (const CTxMemPoolEntry& entry);

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
  void removeConflicts (const CTransaction& tx);

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

/**
 * Utility class that listens to a mempool's removal notifications to track
 * name conflicts.  This is used for DisconnectTip and unit testing.
 */
class CNameConflictTracker
{

private:

  std::shared_ptr<std::vector<CTransactionRef>> txNameConflicts;
  CTxMemPool& pool;

public:

  explicit CNameConflictTracker (CTxMemPool &p);
  ~CNameConflictTracker ();

  inline const std::shared_ptr<const std::vector<CTransactionRef>>
  GetNameConflicts () const
  {
    return txNameConflicts;
  }

  void AddConflictedEntry (CTransactionRef txRemoved);

};

#endif // H_BITCOIN_NAMES_MEMPOOL
