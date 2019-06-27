// Copyright (c) 2014-2019 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <coins.h>
#include <key_io.h>
#include <names/encoding.h>
#include <names/mempool.h>
#include <primitives/transaction.h>
#include <script/names.h>
#include <test/setup_common.h>
#include <txmempool.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

/* No space between BOOST_FIXTURE_TEST_SUITE and '(', so that extraction of
   the test-suite name works with grep as done in the Makefile.  */
BOOST_FIXTURE_TEST_SUITE(name_mempool_tests, TestingSetup)

namespace
{

/**
 * Utility function that returns a sample address script to use in the tests.
 * @return A script that represents a simple address.
 */
CScript
getTestAddress ()
{
  const CTxDestination dest
    = DecodeDestination ("N5e1vXUUL3KfhPyVjQZSes1qQ7eyarDbUU");
  BOOST_CHECK (IsValidDestination (dest));

  return GetScriptForDestination (dest);
}

} // anonymous namespace

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE (name_mempool)
{
  LOCK(mempool.cs);
  mempool.clear ();

  const valtype nameReg = DecodeName ("name-reg", NameEncoding::ASCII);
  const valtype nameUpd = DecodeName ("name-upd", NameEncoding::ASCII);
  const valtype value = DecodeName ("value", NameEncoding::ASCII);
  const valtype valueA = DecodeName ("value-a", NameEncoding::ASCII);
  const valtype valueB = DecodeName ("value-b", NameEncoding::ASCII);
  const CScript addr = getTestAddress ();
  const CScript addr2 = (CScript (addr) << OP_RETURN);

  const valtype rand1(20, 'a');
  const valtype rand2(20, 'b');

  valtype toHash(rand1);
  toHash.insert (toHash.end (), nameReg.begin (), nameReg.end ());
  uint160 hash = Hash160 (toHash);
  const valtype vchHash1(hash.begin (), hash.end ());
  toHash = rand2;
  toHash.insert (toHash.end (), nameReg.begin (), nameReg.end ());
  hash = Hash160 (toHash);
  const valtype vchHash2(hash.begin (), hash.end ());

  const CScript new1
    = CNameScript::buildNameNew (addr, nameReg, rand1);
  const CScript new1p
    = CNameScript::buildNameNew (addr2, nameReg, rand1);
  const CScript new2
    = CNameScript::buildNameNew (addr, nameReg, rand2);
  const CScript first1
    = CNameScript::buildNameFirstupdate (addr, nameReg, value, rand1);
  const CScript first2
    = CNameScript::buildNameFirstupdate (addr, nameReg, value, rand2);
  const CScript upd1 = CNameScript::buildNameUpdate (addr, nameUpd, valueA);
  const CScript upd2 = CNameScript::buildNameUpdate (addr, nameUpd, valueB);

  /* The constructed tx needs not be valid.  We only test
     the mempool acceptance and not validation.  */

  CMutableTransaction mtxNew1;
  mtxNew1.SetNamecoin ();
  mtxNew1.vout.push_back (CTxOut (COIN, new1));
  const CTransaction txNew1(mtxNew1);
  CMutableTransaction mtxNew1p;
  mtxNew1p.SetNamecoin ();
  mtxNew1p.vout.push_back (CTxOut (COIN, new1p));
  const CTransaction txNew1p(mtxNew1p);
  CMutableTransaction mtxNew2;
  mtxNew2.SetNamecoin ();
  mtxNew2.vout.push_back (CTxOut (COIN, new2));
  const CTransaction txNew2(mtxNew2);

  CMutableTransaction mtxReg1;
  mtxReg1.SetNamecoin ();
  mtxReg1.vout.push_back (CTxOut (COIN, first1));
  const CTransaction txReg1(mtxReg1);
  CMutableTransaction mtxReg2;
  mtxReg2.SetNamecoin ();
  mtxReg2.vout.push_back (CTxOut (COIN, first2));
  const CTransaction txReg2(mtxReg2);

  CMutableTransaction mtxUpd1;
  mtxUpd1.SetNamecoin ();
  mtxUpd1.vout.push_back (CTxOut (COIN, upd1));
  const CTransaction txUpd1(mtxUpd1);
  CMutableTransaction mtxUpd2;
  mtxUpd2.SetNamecoin ();
  mtxUpd2.vout.push_back (CTxOut (COIN, upd2));
  const CTransaction txUpd2(mtxUpd2);

  /* Build an invalid transaction.  It should not crash (assert fail)
     the mempool check.  */

  CMutableTransaction mtxInvalid;
  mtxInvalid.SetNamecoin ();
  mempool.checkNameOps (CTransaction (mtxInvalid));

  mtxInvalid.vout.push_back (CTxOut (COIN, new1));
  mtxInvalid.vout.push_back (CTxOut (COIN, new2));
  mtxInvalid.vout.push_back (CTxOut (COIN, first1));
  mtxInvalid.vout.push_back (CTxOut (COIN, first2));
  mtxInvalid.vout.push_back (CTxOut (COIN, upd1));
  mtxInvalid.vout.push_back (CTxOut (COIN, upd2));
  mempool.checkNameOps (CTransaction (mtxInvalid));

  /* For an empty mempool, all tx should be fine.  */
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.updatesName (nameUpd));
  BOOST_CHECK (mempool.checkNameOps (txNew1) && mempool.checkNameOps (txNew1p)
                && mempool.checkNameOps (txNew2));
  BOOST_CHECK (mempool.checkNameOps (txReg1) && mempool.checkNameOps (txReg2));
  BOOST_CHECK (mempool.checkNameOps (txUpd1) && mempool.checkNameOps (txUpd2));

  /* Add name_new's with "stealing" check.  */
  const LockPoints lp;
  const CTxMemPoolEntry entryNew1(MakeTransactionRef(txNew1), 0, 0, 100,
                                  false, 1, lp);
  const CTxMemPoolEntry entryNew2(MakeTransactionRef(txNew2), 0, 0, 100,
                                  false, 1, lp);
  BOOST_CHECK (entryNew1.isNameNew () && entryNew2.isNameNew ());
  BOOST_CHECK (entryNew1.getNameNewHash () == vchHash1
                && entryNew2.getNameNewHash () == vchHash2);
  mempool.addUnchecked (entryNew1);
  mempool.addUnchecked (entryNew2);
  BOOST_CHECK (!mempool.checkNameOps (txNew1p));
  BOOST_CHECK (mempool.checkNameOps (txNew1) && mempool.checkNameOps (txNew2));

  /* Add a name registration.  */
  const CTxMemPoolEntry entryReg(MakeTransactionRef(txReg1), 0, 0, 100,
                                 false, 1, lp);
  BOOST_CHECK (entryReg.isNameRegistration () && !entryReg.isNameUpdate ());
  BOOST_CHECK (entryReg.getName () == nameReg);
  mempool.addUnchecked (entryReg);
  BOOST_CHECK (mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.updatesName (nameReg));
  BOOST_CHECK (!mempool.checkNameOps (txReg2) && mempool.checkNameOps (txUpd1));

  /* Add a name update.  */
  const CTxMemPoolEntry entryUpd(MakeTransactionRef(txUpd1), 0, 0, 100,
                                 false, 1, lp);
  BOOST_CHECK (!entryUpd.isNameRegistration () && entryUpd.isNameUpdate ());
  BOOST_CHECK (entryUpd.getName () == nameUpd);
  mempool.addUnchecked (entryUpd);
  BOOST_CHECK (!mempool.registersName (nameUpd));
  BOOST_CHECK (mempool.updatesName (nameUpd));
  BOOST_CHECK (!mempool.checkNameOps (txUpd2));

  /* Check getTxForName.  */
  BOOST_CHECK (mempool.getTxForName (nameReg) == txReg1.GetHash ());
  BOOST_CHECK (mempool.getTxForName (nameUpd) == txUpd1.GetHash ());

  /* Run mempool sanity check.  */
  CCoinsViewCache view(pcoinsTip.get());
  const CNameScript nameOp(upd1);
  CNameData data;
  data.fromScript (100, COutPoint (uint256 (), 0), nameOp);
  view.SetName (nameUpd, data, false);
  mempool.checkNames (&view);

  /* Remove the transactions again.  */

  mempool.removeRecursive (txReg1);
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (mempool.checkNameOps (txReg1) && mempool.checkNameOps (txReg2));
  BOOST_CHECK (!mempool.checkNameOps (txUpd2));

  mempool.removeRecursive (txUpd1);
  BOOST_CHECK (!mempool.updatesName (nameUpd));
  BOOST_CHECK (mempool.checkNameOps (txUpd1) && mempool.checkNameOps (txUpd2));
  BOOST_CHECK (mempool.checkNameOps (txReg1));

  mempool.removeRecursive (txNew1);
  mempool.removeRecursive (txNew2);
  BOOST_CHECK (!mempool.checkNameOps (txNew1p));
  BOOST_CHECK (mempool.checkNameOps (txNew1) && mempool.checkNameOps (txNew2));

  /* Check getTxForName with non-existent names.  */
  BOOST_CHECK (mempool.getTxForName (nameReg).IsNull ());
  BOOST_CHECK (mempool.getTxForName (nameUpd).IsNull ());

  /* Check removing of conflicted name registrations.  */

  mempool.addUnchecked (entryReg);
  BOOST_CHECK (mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.checkNameOps (txReg2));

  {
    CNameConflictTracker tracker(mempool);
    mempool.removeConflicts (txReg2);
    BOOST_CHECK (tracker.GetNameConflicts ()->size () == 1);
    BOOST_CHECK (tracker.GetNameConflicts ()->front ()->GetHash ()
                  == txReg1.GetHash ());
  }
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (mempool.mapTx.empty ());

  /* Check removing of conflicts after name expiration.  */

  mempool.addUnchecked (entryUpd);
  BOOST_CHECK (mempool.updatesName (nameUpd));
  BOOST_CHECK (!mempool.checkNameOps (txUpd2));

  std::set<valtype> names;
  names.insert (nameUpd);
  {
    CNameConflictTracker tracker(mempool);
    mempool.removeExpireConflicts (names);
    BOOST_CHECK (tracker.GetNameConflicts ()->size () == 1);
    BOOST_CHECK (tracker.GetNameConflicts ()->front ()->GetHash ()
                  == txUpd1.GetHash ());
  }
  BOOST_CHECK (!mempool.updatesName (nameUpd));
  BOOST_CHECK (mempool.mapTx.empty ());

  /* Check removing of conflicts after name unexpiration.  */

  mempool.addUnchecked (entryReg);
  BOOST_CHECK (mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.checkNameOps (txReg2));

  names.clear ();
  names.insert (nameReg);
  {
    CNameConflictTracker tracker(mempool);
    mempool.removeUnexpireConflicts (names);
    BOOST_CHECK (tracker.GetNameConflicts ()->size () == 1);
    BOOST_CHECK (tracker.GetNameConflicts ()->front ()->GetHash ()
                  == txReg1.GetHash ());
  }
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (mempool.mapTx.empty ());
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
