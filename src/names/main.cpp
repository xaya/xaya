// Copyright (c) 2014-2018 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <names/main.h>

#include <chainparams.h>
#include <coins.h>
#include <consensus/validation.h>
#include <hash.h>
#include <dbwrapper.h>
#include <script/interpreter.h>
#include <script/names.h>
#include <txmempool.h>
#include <undo.h>
#include <util.h>
#include <utilstrencodings.h>
#include <validation.h>

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
    view.SetName (name, oldData, true);
}

/* ************************************************************************** */
/* CNameMemPool.  */

uint256
CNameMemPool::getTxForName (const valtype& name) const
{
  NameTxMap::const_iterator mi;

  mi = mapNameRegs.find (name);
  if (mi != mapNameRegs.end ())
    {
      assert (mapNameUpdates.count (name) == 0);
      return mi->second;
    }

  mi = mapNameUpdates.find (name);
  if (mi != mapNameUpdates.end ())
    {
      assert (mapNameRegs.count (name) == 0);
      return mi->second;
    }

  return uint256 ();
}

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

  if (entry.isNameUpdate ())
    {
      const valtype& name = entry.getName ();
      assert (mapNameUpdates.count (name) == 0);
      mapNameUpdates.insert (std::make_pair (name, hash));
    }
}

void
CNameMemPool::remove (const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);

  if (entry.isNameRegistration ())
    {
      const NameTxMap::iterator mit = mapNameRegs.find (entry.getName ());
      assert (mit != mapNameRegs.end ());
      mapNameRegs.erase (mit);
    }
  if (entry.isNameUpdate ())
    {
      const NameTxMap::iterator mit = mapNameUpdates.find (entry.getName ());
      assert (mit != mapNameUpdates.end ());
      mapNameUpdates.erase (mit);
    }
}

void
CNameMemPool::removeConflicts (const CTransaction& tx)
{
  AssertLockHeld (pool.cs);

  for (const auto& txout : tx.vout)
    {
      const CNameScript nameOp(txout.scriptPubKey);
      if (nameOp.isNameOp () && nameOp.getNameOp () == OP_NAME_REGISTER)
        {
          const valtype& name = nameOp.getOpName ();
          const NameTxMap::const_iterator mit = mapNameRegs.find (name);
          if (mit != mapNameRegs.end ())
            {
              const CTxMemPool::txiter mit2 = pool.mapTx.find (mit->second);
              assert (mit2 != pool.mapTx.end ());
              pool.removeRecursive (mit2->GetTx (),
                                    MemPoolRemovalReason::NAME_CONFLICT);
            }
        }
    }
}

void
CNameMemPool::check (const CCoinsView& coins) const
{
  AssertLockHeld (pool.cs);

  std::set<valtype> nameRegs;
  std::set<valtype> nameUpdates;
  for (const auto& entry : pool.mapTx)
    {
      const uint256 txHash = entry.GetTx ().GetHash ();
      if (entry.isNameRegistration ())
        {
          const valtype& name = entry.getName ();

          const NameTxMap::const_iterator mit = mapNameRegs.find (name);
          assert (mit != mapNameRegs.end ());
          assert (mit->second == txHash);

          assert (nameRegs.count (name) == 0);
          nameRegs.insert (name);

          /* There should be no existing name.  */
          CNameData data;
          assert (!coins.GetName (name, data));
        }

      if (entry.isNameUpdate ())
        {
          const valtype& name = entry.getName ();

          const NameTxMap::const_iterator mit = mapNameUpdates.find (name);
          assert (mit != mapNameUpdates.end ());
          assert (mit->second == txHash);

          assert (nameUpdates.count (name) == 0);
          nameUpdates.insert (name);

          CNameData data;
          if (!coins.GetName (name, data))
            assert (false);
        }
    }

  assert (nameRegs.size () == mapNameRegs.size ());
  assert (nameUpdates.size () == mapNameUpdates.size ());

  /* Check that nameRegs and nameUpdates are disjoint.  They must be since
     a name can only be in either category, depending on whether it exists
     at the moment or not.  */
  for (const auto& name : nameRegs)
    assert (nameUpdates.count (name) == 0);
  for (const auto& name : nameUpdates)
    assert (nameRegs.count (name) == 0);
}

bool
CNameMemPool::checkTx (const CTransaction& tx) const
{
  AssertLockHeld (pool.cs);

  /* In principle, multiple name_updates could be performed within the
     mempool at once (building upon each other).  This is disallowed, though,
     since the current mempool implementation does not like it.  (We keep
     track of only a single update tx for each name.)  */

  for (const auto& txout : tx.vout)
    {
      const CNameScript nameOp(txout.scriptPubKey);
      if (!nameOp.isNameOp ())
        continue;

      switch (nameOp.getNameOp ())
        {
        case OP_NAME_REGISTER:
          {
            const valtype& name = nameOp.getOpName ();
            if (registersName (name))
              return false;
            break;
          }

        case OP_NAME_UPDATE:
          {
            const valtype& name = nameOp.getOpName ();
            if (updatesName (name))
              return false;
            break;
          }

        default:
          assert (false);
        }
    }

  return true;
}

/* ************************************************************************** */
/* CNameConflictTracker.  */

namespace
{

void
ConflictTrackerNotifyEntryRemoved (CNameConflictTracker* tracker,
                                   CTransactionRef txRemoved,
                                   MemPoolRemovalReason reason)
{
  if (reason == MemPoolRemovalReason::NAME_CONFLICT)
    tracker->AddConflictedEntry (txRemoved);
}

} // anonymous namespace

CNameConflictTracker::CNameConflictTracker (CTxMemPool &p)
  : txNameConflicts(std::make_shared<std::vector<CTransactionRef>>()), pool(p)
{
  pool.NotifyEntryRemoved.connect (
    boost::bind (&ConflictTrackerNotifyEntryRemoved, this, _1, _2));
}

CNameConflictTracker::~CNameConflictTracker ()
{
  pool.NotifyEntryRemoved.disconnect (
    boost::bind (&ConflictTrackerNotifyEntryRemoved, this, _1, _2));
}

void
CNameConflictTracker::AddConflictedEntry (CTransactionRef txRemoved)
{
  txNameConflicts->emplace_back (std::move (txRemoved));
}

/* ************************************************************************** */

bool
CheckNameTransaction (const CTransaction& tx, unsigned nHeight,
                      const CCoinsView& view,
                      CValidationState& state)
{
  const std::string strTxid = tx.GetHash ().GetHex ();
  const char* txid = strTxid.c_str ();

  /* As a first step, try to locate inputs and outputs of the transaction
     that are name scripts.  At most one input and output should be
     a name operation.  */

  int nameIn = -1;
  CNameScript nameOpIn;
  Coin coinIn;
  for (unsigned i = 0; i < tx.vin.size (); ++i)
    {
      const COutPoint& prevout = tx.vin[i].prevout;
      Coin coin;
      if (!view.GetCoin (prevout, coin))
        return error ("%s: failed to fetch input coin for %s", __func__, txid);

      const CNameScript op(coin.out.scriptPubKey);
      if (op.isNameOp ())
        {
          if (nameIn != -1)
            return state.Invalid (error ("%s: multiple name inputs into"
                                         " transaction %s", __func__, txid));
          nameIn = i;
          nameOpIn = op;
          coinIn = coin;
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
            return state.Invalid (error ("%s: multiple name outputs from"
                                         " transaction %s", __func__, txid));
          nameOut = i;
          nameOpOut = op;
        }
    }

  /* If there are no name outputs, then this transaction is not a name
     operation.  In this case, there should also be no name inputs, but
     otherwise the validation is done.  */
  if (nameOut == -1)
    {
      if (nameIn != -1)
        return state.Invalid (error ("%s: tx %s has name inputs but no outputs",
                                     __func__, txid));
      return true;
    }

  /* Reject "greedy names".  */
  const Consensus::Params& params = Params ().GetConsensus ();
  if (tx.vout[nameOut].nValue < params.rules->MinNameCoinAmount(nHeight))
    return state.Invalid (error ("%s: greedy name", __func__));

  /* Now that we have ruled out NAME_NEW, check that we have a previous
     name input that is being updated.  */

  assert (nameOpOut.isAnyUpdate ());
  if (nameOpOut.getNameOp () == OP_NAME_REGISTER) {
    if (nameIn != -1)
      return state.Invalid (error ("%s: name registration with"
                                   " name input", __func__));
  }
  else if (nameIn == -1)
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
        return state.Invalid (error ("%s: NAME_UPDATE name mismatch to prev tx"
                                     " found in %s", __func__, txid));

      /* This is actually redundant, since updates need an existing name coin
         to spend anyway.  But it does not hurt to enforce this here, too.  */
      CNameData oldName;
      if (!view.GetName (name, oldName))
        return state.Invalid (error ("%s: NAME_UPDATE name does not exist",
                                     __func__));

      /* This is an internal consistency check.  If everything is fine,
         the input coins from the UTXO database should match the
         name database.  */
      assert (static_cast<unsigned> (coinIn.nHeight) == oldName.getHeight ());
      assert (tx.vin[nameIn].prevout == oldName.getUpdateOutpoint ());

      return true;
    }

  /* Finally, NAME_REGISTER.  */

  CNameData oldName;
  if (view.GetName (name, oldName))
    return state.Invalid (error ("CheckNameTransaction: NAME_REGISTER"
                                 " on an existing name"));

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

  /* Changes are encoded in the outputs.  We don't have to do any checks,
     so simply apply all these.  */

  for (unsigned i = 0; i < tx.vout.size (); ++i)
    {
      const CNameScript op(tx.vout[i].scriptPubKey);
      if (op.isNameOp () && op.isAnyUpdate ())
        {
          const valtype& name = op.getOpName ();
          LogPrint (BCLog::NAMES, "Updating name at height %d: %s\n",
                    nHeight, ValtypeToString (name).c_str ());

          CNameTxUndo opUndo;
          opUndo.fromOldState (name, view);
          undo.vnameundo.push_back (opUndo);

          CNameData data;
          data.fromScript (nHeight, COutPoint (tx.GetHash (), i), op);
          view.SetName (name, data, false);
        }
    }
}

void
CheckNameDB (bool disconnect)
{
  const int option
    = gArgs.GetArg ("-checknamedb", Params ().DefaultCheckNameDB ());

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
