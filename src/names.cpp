// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names.h"

#include "chainparams.h"
#include "coins.h"
#include "leveldbwrapper.h"
#include "txmempool.h"
#include "undo.h"

#include "script/names.h"

#include <set>

/**
 * Check whether a name at nPrevHeight is expired at nHeight.  Also
 * heights of MEMPOOL_HEIGHT are supported.  For nHeight == MEMPOOL_HEIGHT,
 * we check at the current best tip's height.
 * @param nPrevHeight The name's output.
 * @param nHeight The height at which it should be updated.
 * @return True iff the name is expired.
 */
static bool
isExpired (unsigned nPrevHeight, unsigned nHeight)
{
  if (nPrevHeight == MEMPOOL_HEIGHT)
    {
      assert (nHeight == MEMPOOL_HEIGHT);
      return false;
    }

  if (nHeight == MEMPOOL_HEIGHT)
    nHeight = chainActive.Height ();

  return nPrevHeight + Params ().NameExpirationDepth (nHeight) <= nHeight;
}

/* ************************************************************************** */
/* CNameData.  */

bool
CNameData::isExpired () const
{
  return isExpired (chainActive.Height ());
}

bool
CNameData::isExpired (unsigned h) const
{
  return ::isExpired (nHeight, h);
}

void
CNameData::fromScript (unsigned h, const COutPoint& out,
                       const CNameScript& script)
{
  assert (script.isAnyUpdate ());
  value = script.getOpValue ();
  nHeight = h;
  prevout = out;
  addr = script.getAddress ();
}

/* ************************************************************************** */
/* CNameCache.  */

/* Try to get a name's associated data.  This looks only
   in entries, and doesn't care about deleted data.  */
bool
CNameCache::get (const valtype& name, CNameData& data) const
{
  const std::map<valtype, CNameData>::const_iterator i = entries.find (name);
  if (i == entries.end ())
    return false;

  data = i->second;
  return true;
}

void
CNameCache::updateNamesForHeight (unsigned nHeight,
                                  std::set<valtype>& names) const
{
  /* Seek in the map of cached entries to the first one corresponding
     to our height.  */

  const ExpireEntry seekEntry(nHeight, valtype ());
  std::map<ExpireEntry, bool>::const_iterator it;

  for (it = expireIndex.lower_bound (seekEntry); it != expireIndex.end (); ++it)
    {
      const ExpireEntry& cur = it->first;
      assert (cur.first >= nHeight);
      if (cur.first > nHeight)
        break;

      if (it->second)
        names.insert (cur.second);
      else
        names.erase (cur.second);
    }
}

/* Insert (or update) a name.  If it is marked as "deleted", this also
   removes the "deleted" mark.  */
void
CNameCache::set (const valtype& name, const CNameData& data)
{
  const std::set<valtype>::iterator di = deleted.find (name);
  if (di != deleted.end ())
    deleted.erase (di);

  const std::map<valtype, CNameData>::iterator ei = entries.find (name);
  if (ei != entries.end ())
    ei->second = data;
  else
    entries.insert (std::make_pair (name, data));
}

/* Delete a name.  If it is in the "entries" set also, remove it there.  */
void
CNameCache::remove (const valtype& name)
{
  const std::map<valtype, CNameData>::iterator ei = entries.find (name);
  if (ei != entries.end ())
    entries.erase (ei);

  deleted.insert (name);
}

void
CNameCache::addExpireIndex (const valtype& name, unsigned height)
{
  const ExpireEntry entry(height, name);
  expireIndex[entry] = true;
}

void
CNameCache::removeExpireIndex (const valtype& name, unsigned height)
{
  const ExpireEntry entry(height, name);
  expireIndex[entry] = false;
}

/* Apply all the changes in the passed-in record on top of this one.  */
void
CNameCache::apply (const CNameCache& cache)
{
  for (std::map<valtype, CNameData>::const_iterator i = cache.entries.begin ();
       i != cache.entries.end (); ++i)
    set (i->first, i->second);

  for (std::set<valtype>::const_iterator i = cache.deleted.begin ();
       i != cache.deleted.end (); ++i)
    remove (*i);

  for (std::map<ExpireEntry, bool>::const_iterator i
        = cache.expireIndex.begin (); i != cache.expireIndex.end (); ++i)
    expireIndex[i->first] = i->second;
}

/* Write all cached changes to a database batch update object.  */
void
CNameCache::writeBatch (CLevelDBBatch& batch) const
{
  for (std::map<valtype, CNameData>::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    batch.Write (std::make_pair ('n', i->first), i->second);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    batch.Erase (std::make_pair ('n', *i));

  for (std::map<ExpireEntry, bool>::const_iterator i = expireIndex.begin ();
       i != expireIndex.end (); ++i)
    if (i->second)
      batch.Write (std::make_pair ('x', i->first));
    else
      batch.Erase (std::make_pair ('x', i->first));
}

/* ************************************************************************** */
/* CNameTxUndo.  */

void
CNameTxUndo::fromOldState (const valtype& nm, const CCoinsView& view)
{
  name = nm;
  isNew = !view.GetName (name, oldData);
}

void
CNameTxUndo::apply (CCoinsViewCache& view) const
{
  if (isNew)
    view.DeleteName (name);
  else
    view.SetName (name, oldData);
}

/* ************************************************************************** */
/* CNameMemPool.  */

void
CNameMemPool::addUnchecked (const uint256& hash, const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);

  if (entry.isNameRegistration ())
    {
      const valtype& name = entry.getName ();
      assert (mapNameRegs.count (name) == 0);
      mapNameRegs.insert (std::make_pair (name, hash));
    }
}

void
CNameMemPool::remove (const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);

  if (entry.isNameRegistration ())
    {
      const std::map<valtype, uint256>::iterator mit
        = mapNameRegs.find (entry.getName ());
      assert (mit != mapNameRegs.end ());
      mapNameRegs.erase (mit);
    }
}

void
CNameMemPool::removeConflicts (const CTransaction& tx,
                               std::list<CTransaction>& removed)
{
  AssertLockHeld (pool.cs);

  if (!tx.IsNamecoin ())
    return;

  BOOST_FOREACH (const CTxOut& txout, tx.vout)
    {
      const CNameScript nameOp(txout.scriptPubKey);
      if (nameOp.isNameOp () && nameOp.getNameOp () == OP_NAME_FIRSTUPDATE)
        {
          const valtype& name = nameOp.getOpName ();
          const std::map<valtype, uint256>::const_iterator mit
            = mapNameRegs.find (name);
          if (mit != mapNameRegs.end ())
            {
              const std::map<uint256, CTxMemPoolEntry>::const_iterator mit2
                = pool.mapTx.find (mit->second);
              assert (mit2 != pool.mapTx.end ());
              pool.remove (mit2->second.GetTx (), removed, true);
            }
        }
    }
}

void
CNameMemPool::check (const CCoinsView& coins) const
{
  AssertLockHeld (pool.cs);

  const uint256 blockHash = coins.GetBestBlock ();
  int nHeight;
  if (blockHash == 0)
    nHeight = 0;
  else
    nHeight = mapBlockIndex.find (blockHash)->second->nHeight;

  std::set<valtype> nameRegs;
  BOOST_FOREACH (const PAIRTYPE(const uint256, CTxMemPoolEntry)& entry,
                 pool.mapTx)
    {
      if (entry.second.isNameRegistration ())
        {
          const valtype& name = entry.second.getName ();

          const std::map<valtype, uint256>::const_iterator mit
            = mapNameRegs.find (name);
          assert (mit != mapNameRegs.end ());
          assert (mit->second == entry.first);

          assert (nameRegs.count (name) == 0);
          nameRegs.insert (name);

          /* FIXME: In a rare situation, it could happen that a name
             expires, a re-registration tx goes in, and then the
             chain is disconnected so that the name is no longer
             expired.  In that case, the check below could fail
             even though things are mostly "fine".  */
          CNameData data;
          if (coins.GetName (name, data))
            assert (data.isExpired (nHeight));
        }

      /* TODO: Also check name updates against expired names.  For this,
         think about a way to implement removal of expired outputs from
         the UTXO set.  If this is done, the check already done against
         spent outputs should be enough?  */
    }

  assert (nameRegs.size () == mapNameRegs.size ());
}

bool
CNameMemPool::checkTx (const CTransaction& tx) const
{
  AssertLockHeld (pool.cs);

  if (!tx.IsNamecoin ())
    return true;

  BOOST_FOREACH (const CTxOut& txout, tx.vout)
    {
      const CNameScript nameOp(txout.scriptPubKey);
      if (nameOp.isNameOp () && nameOp.getNameOp () == OP_NAME_FIRSTUPDATE)
        {
          const valtype& name = nameOp.getOpName ();
          if (registersName (name))
            return false;
        }
    }

  return true;
}

/* ************************************************************************** */

bool
CheckNameTransaction (const CTransaction& tx, unsigned nHeight,
                      const CCoinsView& view,
                      CValidationState& state)
{
  /* As a first step, try to locate inputs and outputs of the transaction
     that are name scripts.  At most one input and output should be
     a name operation.  */

  int nameIn = -1;
  CNameScript nameOpIn;
  CCoins coinsIn;
  for (unsigned i = 0; i < tx.vin.size (); ++i)
    {
      const COutPoint& prevout = tx.vin[i].prevout;
      CCoins coins;
      if (!view.GetCoins (prevout.hash, coins))
        return error ("CheckNameTransaction: failed to fetch input coins");

      const CNameScript op(coins.vout[prevout.n].scriptPubKey);
      if (op.isNameOp ())
        {
          if (nameIn != -1)
            return state.Invalid (error ("CheckNameTransaction: multiple name"
                                         " inputs into transaction"));
          nameIn = i;
          nameOpIn = op;
          coinsIn = coins;
        }
    }

  int nameOut = -1;
  CNameScript nameOpOut;
  for (unsigned i = 0; i < tx.vout.size (); ++i)
    {
      const CNameScript op(tx.vout[i].scriptPubKey);
      if (op.isNameOp ())
        {
          if (nameOut != -1)
            return state.Invalid (error ("CheckNameTransaction: multiple name"
                                         " outputs from transaction"));
          nameOut = i;
          nameOpOut = op;
        }
    }

  /* Check that no name inputs/outputs are present for a non-Namecoin tx.
     If that's the case, all is fine.  For a Namecoin tx instead, there
     should be at least an output (for NAME_NEW, no inputs are expected).  */

  if (!tx.IsNamecoin ())
    {
      if (nameIn != -1 || nameOut != -1)
        return state.Invalid (error ("CheckNameTransaction: non-Namecoin tx"
                                     " has name inputs/outputs"));
      return true;
    }

  assert (tx.IsNamecoin ());
  if (nameOut == -1)
    return state.Invalid (error ("CheckNameTransaction: Namecoin tx has no"
                                 " name outputs"));

  /* Handle NAME_NEW now, since this is easy and different from the other
     operations.  */

  if (nameOpOut.getNameOp () == OP_NAME_NEW)
    {
      if (nameIn != -1)
        return state.Invalid (error ("CheckNameTransaction: NAME_NEW with"
                                     " previous name input"));

      if (nameOpOut.getOpHash ().size () != 20)
        return state.Invalid (error ("CheckNameTransaction: NAME_NEW's hash"
                                     " has wrong size"));

      return true;
    }

  /* Now that we have ruled out NAME_NEW, check that we have a previous
     name input that is being updated.  */

  assert (nameOpOut.isAnyUpdate ());
  if (nameIn == -1)
    return state.Invalid (error ("CheckNameTransaction: update without"
                                 " previous name input"));
  const valtype& name = nameOpOut.getOpName ();

  if (name.size () > MAX_NAME_LENGTH)
    return state.Invalid (error ("CheckNameTransaction: name too long"));
  if (nameOpOut.getOpValue ().size () > MAX_VALUE_LENGTH)
    return state.Invalid (error ("CheckNameTransaction: value too long"));

  /* Process NAME_UPDATE next.  */

  if (nameOpOut.getNameOp () == OP_NAME_UPDATE)
    {
      if (!nameOpIn.isAnyUpdate ())
        return state.Invalid (error ("CheckNameTransaction: NAME_UPDATE with"
                                     " prev input that is no update"));

      if (name != nameOpIn.getOpName ())
        return state.Invalid (error ("CheckNameTransaction: NAME_UPDATE name"
                                     " mismatch to prev tx"));

      if (isExpired (coinsIn.nHeight, nHeight))
        return state.Invalid (error ("CheckNameTransaction: trying to update"
                                     " expired name"));

      return true;
    }

  /* Finally, NAME_FIRSTUPDATE.  */

  assert (nameOpOut.getNameOp () == OP_NAME_FIRSTUPDATE);
  if (nameOpIn.getNameOp () != OP_NAME_NEW)
    return state.Invalid (error ("CheckNameTransaction: NAME_FIRSTUPDATE"
                                 " with non-NAME_NEW prev tx"));

  /* Maturity of NAME_NEW is checked only if we're not adding
     to the mempool.  */
  if (nHeight != MEMPOOL_HEIGHT)
    {
      assert (static_cast<unsigned> (coinsIn.nHeight) != MEMPOOL_HEIGHT);
      if (coinsIn.nHeight + MIN_FIRSTUPDATE_DEPTH > nHeight)
        return state.Invalid (error ("CheckNameTransaction: NAME_NEW"
                                     " is not mature for FIRST_UPDATE"));
    }

  if (nameOpOut.getOpRand ().size () > 20)
    return state.Invalid (error ("CheckNameTransaction: NAME_FIRSTUPDATE"
                                 " rand too large, %d bytes",
                                 nameOpOut.getOpRand ().size ()));

  {
    valtype toHash(nameOpOut.getOpRand ());
    toHash.insert (toHash.end (), name.begin (), name.end ());
    const uint160 hash = Hash160 (toHash);
    if (hash != uint160 (nameOpIn.getOpHash ()))
      return state.Invalid (error ("CheckNameTransaction: NAME_FIRSTUPDATE"
                                   " hash mismatch"));
  }

  CNameData oldName;
  if (view.GetName (name, oldName) && !oldName.isExpired (nHeight))
    return state.Invalid (error ("CheckNameTransaction: NAME_FIRSTUPDATE"
                                 " on an unexpired name"));

  /* We don't have to specifically check that miners don't create blocks with
     conflicting NAME_FIRSTUPDATE's, since the mining's CCoinsViewCache
     takes care of this with the check above already.  */

  return true;
}

void
ApplyNameTransaction (const CTransaction& tx, unsigned nHeight,
                      CCoinsViewCache& view, CBlockUndo& undo)
{
  assert (nHeight != MEMPOOL_HEIGHT);
  if (!tx.IsNamecoin ())
    return;

  /* Changes are encoded in the outputs.  We don't have to do any checks,
     so simply apply all these.  */

  for (unsigned i = 0; i < tx.vout.size (); ++i)
    {
      const CNameScript op(tx.vout[i].scriptPubKey);
      if (op.isNameOp () && op.isAnyUpdate ())
        {
          const valtype& name = op.getOpName ();
          LogPrintf ("Updating name at height %d: %s\n",
                     nHeight, ValtypeToString (name).c_str ());

          CNameTxUndo opUndo;
          opUndo.fromOldState (name, view);
          undo.vnameundo.push_back (opUndo);

          CNameData data;
          data.fromScript (nHeight, COutPoint (tx.GetHash (), i), op);
          view.SetName (name, data);
        }
    }
}

void
CheckNameDB (bool disconnect)
{
  const int option = GetArg ("-checknamedb", Params ().DefaultCheckNameDB ());

  if (option == -1)
    return;

  assert (option >= 0);
  if (option != 0)
    {
      if (disconnect || chainActive.Height () % option != 0)
        return;
    }

  pcoinsTip->Flush ();
  assert (pcoinsTip->ValidateNameDB ());
}
