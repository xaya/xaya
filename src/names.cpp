// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names.h"

#include "coins.h"
#include "leveldbwrapper.h"
#include "main.h"
#include "txmempool.h"

#include "core/transaction.h"

#include "script/names.h"

static const unsigned MAX_VALUE_LENGTH = 1023;
static const unsigned MAX_NAME_LENGTH = 255;
static const unsigned MIN_FIRSTUPDATE_DEPTH = 12;

/**
 * Return the expiration depth at the given height.
 * @param nHeight Height at which to look.
 * @return The expiration depth at that height.
 */
static unsigned
getExpirationDepth (unsigned nHeight)
{
  if (nHeight < 24000)
    return 12000;
  if (nHeight < 48000)
    return nHeight - 12000;

  return 36000;
}

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

  return nPrevHeight <= nHeight - getExpirationDepth (nHeight);
}

/* ************************************************************************** */
/* CNameData.  */

void
CNameData::fromScript (unsigned h, const CNameScript& script)
{
  assert (script.isAnyUpdate ());
  value = script.getOpValue ();
  nHeight = h;
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
                                 " rand too large"));

  {
    valtype toHash(nameOpOut.getOpRand ());
    toHash.insert (toHash.end (), name.begin (), name.end ());
    const uint160 hash = Hash160 (toHash);
    if (hash != uint160 (nameOpIn.getOpHash ()))
      return state.Invalid (error ("CheckNameTransaction: NAME_FIRSTUPDATE"
                                   " hash mismatch"));
  }

  CNameData oldName;
  if (view.GetName (name, oldName)
      && !isExpired (oldName.getHeight (), nHeight))
    return state.Invalid (error ("CheckNameTransaction: NAME_FIRSTUPDATE"
                                 " on an unexpired name"));

  /* We don't have to specifically check that miners don't create blocks with
     conflicting NAME_FIRSTUPDATE's, since the mining's CCoinsViewCache
     takes care of this with the check above already.  */

  return true;
}
