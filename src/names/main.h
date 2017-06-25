// Copyright (c) 2014-2017 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES_MAIN
#define H_BITCOIN_NAMES_MAIN

#include "amount.h"
#include "names/common.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>

class CBlockUndo;
class CCoinsView;
class CCoinsViewCache;
class CTxMemPool;
class CTxMemPoolEntry;
class CValidationState;

/* Some constants defining name limits.  */
static const unsigned MAX_VALUE_LENGTH = 1023;
static const unsigned MAX_NAME_LENGTH = 255;
static const unsigned MIN_FIRSTUPDATE_DEPTH = 12;
static const unsigned MAX_VALUE_LENGTH_UI = 520;

/** The amount of coins to lock in created transactions.  */
static const CAmount NAME_LOCKED_AMOUNT = COIN / 100;

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
    inline void SerializationOp (Stream& s, Operation ser_action)
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

  /** Type used for internal indices.  */
  typedef std::map<valtype, uint256> NameTxMap;

  /**
   * Keep track of names that are registered by transactions in the pool.
   * Map name to registering transaction.
   */
  NameTxMap mapNameRegs;

  /** Map pending name updates to transaction IDs.  */
  NameTxMap mapNameUpdates;

  /**
   * Map NAME_NEW hashes to the corresponding transaction IDs.  This is
   * data that is kept only in memory but never cleared (until a restart).
   * It is used to prevent "name_new stealing", at least in a "soft" way.
   */
  NameTxMap mapNameNews;

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
  void removeConflicts (const CTransaction& tx);

  /**
   * Remove conflicts in the mempool due to unexpired names.  This removes
   * conflicting name registrations that are no longer possible.
   * @param unexpired The set of unexpired names.
   * @param removed Put removed tx here.
   */
  void removeUnexpireConflicts (const std::set<valtype>& unexpired);
  /**
   * Remove conflicts in the mempool due to expired names.  This removes
   * conflicting name updates that are no longer possible.
   * @param expired The set of expired names.
   * @param removed Put removed tx here.
   */
  void removeExpireConflicts (const std::set<valtype>& expired);

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
/* CNameConflictTracker.  */

/**
 * Utility class that listens to a mempool's removal notifications to track
 * name conflicts.  This is used for DisconnectTip and unit testing.
 */
class CNameConflictTracker
{

private:

  std::vector<CTransactionRef> txNameConflicts;
  CTxMemPool& pool;

public:

  explicit CNameConflictTracker (CTxMemPool &p);
  ~CNameConflictTracker ();

  inline const std::vector<CTransactionRef>&
  GetNameConflicts () const
  {
    return txNameConflicts;
  }

  void AddConflictedEntry (CTransactionRef txRemoved);

};

/* ************************************************************************** */

/**
 * Check a transaction according to the additional Namecoin rules.  This
 * ensures that all name operations (if any) are valid and that it has
 * name operations iff it is marked as Namecoin tx by its version.
 * @param tx The transaction to check.
 * @param nHeight Height at which the tx will be.
 * @param view The current chain state.
 * @param state Resulting validation state.
 * @param flags Verification flags.
 * @return True in case of success.
 */
bool CheckNameTransaction (const CTransaction& tx, unsigned nHeight,
                           const CCoinsView& view,
                           CValidationState& state, unsigned flags);

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
bool UnexpireNames (unsigned nHeight, CBlockUndo& undo,
                    CCoinsViewCache& view, std::set<valtype>& names);

/**
 * Check the name database consistency.  This calls CCoinsView::ValidateNameDB,
 * but only if applicable depending on the -checknamedb setting.  If it fails,
 * this throws an assertion failure.
 * @param disconnect Whether we are disconnecting blocks.
 */
void CheckNameDB (bool disconnect);

#endif // H_BITCOIN_NAMES_MAIN
