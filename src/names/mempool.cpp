// Copyright (c) 2014-2023 Daniel Kraft
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

unsigned
CNameMemPool::pendingChainLength (const valtype& name) const
{
  unsigned res = 0;
  if (registersName (name))
    ++res;

  const auto mit = updates.find (name);
  if (mit != updates.end ())
    res += mit->second.size ();

  return res;
}

namespace
{

/**
 * Returns the outpoint matching the name operation in a given mempool tx, if
 * there is any.  The txid must be for an entry in the mempool.
 */
COutPoint
getNameOutput (const CTxMemPool& pool, const Txid& txid)
{
  AssertLockHeld (pool.cs);

  const auto mit = pool.mapTx.find (txid);
  assert (mit != pool.mapTx.end ());
  const auto& vout = mit->GetTx ().vout;

  for (unsigned i = 0; i != vout.size (); ++i)
    {
      const CNameScript nameOp(vout[i].scriptPubKey);
      if (nameOp.isNameOp ())
        return COutPoint (txid, i);
    }

  return COutPoint ();
}

} // anonymous namespace

COutPoint
CNameMemPool::lastNameOutput (const valtype& name) const
{
  const auto itUpd = updates.find (name);
  if (itUpd != updates.end ())
    {
      /* From all the pending updates, we have to find the last one.  This is
         the unique outpoint that is not also spent by some other transaction.
         Thus, we keep track of all the transactions spent as well, and then
         remove those from the sets of candidates.  Doing so by txid (rather
         than outpoint) is enough, as those transactions must be in a "chain"
         anyway.  */

      const std::set<Txid>& candidateTxids = itUpd->second;
      std::set<Txid> spentTxids;

      for (const auto& txid : candidateTxids)
        {
          const auto mit = pool.mapTx.find (txid);
          assert (mit != pool.mapTx.end ());
          for (const auto& in : mit->GetTx ().vin)
            spentTxids.insert (in.prevout.hash);
        }

      COutPoint res;
      for (const auto& txid : candidateTxids)
        {
          if (spentTxids.count (txid) > 0)
            continue;

          assert (res.IsNull ());
          res = getNameOutput (pool, txid);
        }

      assert (!res.IsNull ());
      return res;
    }

  const auto itReg = mapNameRegs.find (name);
  if (itReg != mapNameRegs.end ())
    return getNameOutput (pool, itReg->second);

  return COutPoint ();
}

void
CNameMemPool::addUnchecked (const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);
  const Txid& txHash = entry.GetTx ().GetHash ();

  if (entry.isNameRegistration ())
    {
      const valtype& name = entry.getName ();
      assert (mapNameRegs.count (name) == 0);
      mapNameRegs.insert (std::make_pair (name, txHash));
    }

  if (entry.isNameUpdate ())
    {
      const valtype& name = entry.getName ();
      const auto mit = updates.find (name);

      if (mit == updates.end ())
        updates.emplace (name, std::set<Txid> ({txHash}));
      else
        mit->second.insert (txHash);
    }
}

void
CNameMemPool::remove (const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);

  if (entry.isNameRegistration ())
    {
      const auto mit = mapNameRegs.find (entry.getName ());
      assert (mit != mapNameRegs.end ());
      mapNameRegs.erase (mit);
    }

  if (entry.isNameUpdate ())
    {
      const auto itName = updates.find (entry.getName ());
      assert (itName != updates.end ());
      auto& txids = itName->second;
      const auto itTxid = txids.find (entry.GetTx ().GetHash ());
      assert (itTxid != txids.end ());
      txids.erase (itTxid);
      if (txids.empty ())
        updates.erase (itName);
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
          const auto mit = mapNameRegs.find (name);
          if (mit != mapNameRegs.end ())
            {
              const auto mit2 = pool.mapTx.find (mit->second);
              assert (mit2 != pool.mapTx.end ());
              pool.removeRecursive (mit2->GetTx (),
                                    MemPoolRemovalReason::NAME_CONFLICT);
            }
        }
    }
}

void
CNameMemPool::check (const CCoinsViewCache& tip) const
{
  AssertLockHeld (pool.cs);

  std::set<valtype> nameRegs;
  std::map<valtype, unsigned> nameUpdates;
  for (const auto& entry : pool.mapTx)
    {
      const Txid txHash = entry.GetTx ().GetHash ();
      if (entry.isNameRegistration ())
        {
          const valtype& name = entry.getName ();

          const auto mit = mapNameRegs.find (name);
          assert (mit != mapNameRegs.end ());
          assert (mit->second == txHash);

          assert (nameRegs.count (name) == 0);
          nameRegs.insert (name);

          /* There should be no existing name.  */
          CNameData data;
          assert (!tip.GetName (name, data));
        }

      if (entry.isNameUpdate ())
        {
          const valtype& name = entry.getName ();

          const auto mit = updates.find (name);
          assert (mit != updates.end ());
          assert (mit->second.count (txHash) > 0);

          ++nameUpdates[name];

          CNameData data;
          if (!tip.GetName (name, data))
            assert (registersName (name));
        }
    }

  assert (nameRegs.size () == mapNameRegs.size ());
  assert (nameUpdates.size () == updates.size ());
  for (const auto& upd : nameUpdates)
    assert (updates.at (upd.first).size () == upd.second);
}

bool
CNameMemPool::checkTx (const CTransaction& tx) const
{
  AssertLockHeld (pool.cs);

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
          /* Multiple updates of the same name in a chain are perfectly fine.
             The main mempool logic takes care that updates are ordered
             properly and really a chain, as this is automatic due to the
             coloured-coin nature of names.  */
          break;

        default:
          assert (false);
        }
    }

  return true;
}
