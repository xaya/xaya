// Copyright (c) 2014-2019 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <names/mempool.h>

#include <coins.h>
#include <logging.h>
#include <names/encoding.h>
#include <script/names.h>
#include <txmempool.h>
#include <util/strencodings.h>
#include <validation.h>

/* ************************************************************************** */

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
CNameMemPool::addUnchecked (const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);
  const uint256& txHash = entry.GetTx ().GetHash ();

  if (entry.isNameRegistration ())
    {
      const valtype& name = entry.getName ();
      assert (mapNameRegs.count (name) == 0);
      mapNameRegs.insert (std::make_pair (name, txHash));
    }

  if (entry.isNameUpdate ())
    {
      const valtype& name = entry.getName ();
      assert (mapNameUpdates.count (name) == 0);
      mapNameUpdates.insert (std::make_pair (name, txHash));
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
