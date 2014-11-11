// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "coins.h"
#include "main.h"
#include "names.h"
#include "txmempool.h"

#include "core/transaction.h"

#include "script/names.h"

#include <boost/test/unit_test.hpp>

#include <list>

#include <stdint.h>

BOOST_AUTO_TEST_SUITE (name_tests)

/**
 * Utility function that returns a sample address script to use in the tests.
 * @return A script that represents a simple address.
 */
static CScript
getTestAddress ()
{
  CBitcoinAddress addr("N5e1vXUUL3KfhPyVjQZSes1qQ7eyarDbUU");
  BOOST_CHECK (addr.IsValid ());

  return GetScriptForDestination (addr.Get ());
}

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE (name_scripts)
{
  const CScript addr = getTestAddress ();
  const CNameScript opNone(addr);
  BOOST_CHECK (!opNone.isNameOp ());
  BOOST_CHECK (opNone.getAddress () == addr);

  const valtype name = ValtypeFromString ("my-cool-name");
  const valtype value = ValtypeFromString ("42!");

  const valtype rand(20, 'x');
  valtype toHash(rand);
  toHash.insert (toHash.end (), name.begin (), name.end ());
  const uint160 hash = Hash160 (toHash);

  CScript script;
  script = CNameScript::buildNameNew (addr, hash);
  const CNameScript opNew(script);
  BOOST_CHECK (opNew.isNameOp ());
  BOOST_CHECK (opNew.getAddress () == addr);
  BOOST_CHECK (!opNew.isAnyUpdate ());
  BOOST_CHECK (opNew.getNameOp () == OP_NAME_NEW);
  BOOST_CHECK (uint160 (opNew.getOpHash ()) == hash);

  script = CNameScript::buildNameFirstupdate (addr, name, value, rand);
  const CNameScript opFirstupdate(script);
  BOOST_CHECK (opFirstupdate.isNameOp ());
  BOOST_CHECK (opFirstupdate.getAddress () == addr);
  BOOST_CHECK (opFirstupdate.isAnyUpdate ());
  BOOST_CHECK (opFirstupdate.getNameOp () == OP_NAME_FIRSTUPDATE);
  BOOST_CHECK (opFirstupdate.getOpName () == name);
  BOOST_CHECK (opFirstupdate.getOpValue () == value);
  BOOST_CHECK (opFirstupdate.getOpRand () == rand);

  script = CNameScript::buildNameUpdate (addr, name, value);
  const CNameScript opUpdate(script);
  BOOST_CHECK (opUpdate.isNameOp ());
  BOOST_CHECK (opUpdate.getAddress () == addr);
  BOOST_CHECK (opUpdate.isAnyUpdate ());
  BOOST_CHECK (opUpdate.getNameOp () == OP_NAME_UPDATE);
  BOOST_CHECK (opUpdate.getOpName () == name);
  BOOST_CHECK (opUpdate.getOpValue () == value);
}

/* ************************************************************************** */

class CTestNameWalker : public CNameWalker
{

public:

  std::set<valtype> names;
  bool single;

  bool nextName (const valtype& name, const CNameData& data);

};

bool
CTestNameWalker::nextName (const valtype& name, const CNameData&)
{
  assert (names.count (name) == 0);
  names.insert (name);

  return !single;
}

BOOST_AUTO_TEST_CASE (name_database)
{
  const valtype name1 = ValtypeFromString ("database-test-name-1");
  const valtype name2 = ValtypeFromString ("database-test-name-2");
  const valtype value = ValtypeFromString ("my-value");
  const CScript addr = getTestAddress ();

  /* Choose two height values.  To verify that serialisation of the
     expiration entries works as it should, make sure that the values
     are ordered wrongly when serialised in little-endian.  */
  const unsigned height1 = 0x00ff;
  const unsigned height2 = 0x0142;

  CNameData dataHeight1, dataHeight2, data2;
  CScript updateScript = CNameScript::buildNameUpdate (addr, name1, value);
  const CNameScript nameOp(updateScript);
  dataHeight1.fromScript (height1, COutPoint (uint256 (), 0), nameOp);
  dataHeight2.fromScript (height2, COutPoint (uint256 (), 0), nameOp);

  std::set<valtype> setExpected, setRet;

  CCoinsViewCache& view = *pcoinsTip;

  setExpected.clear ();
  setRet.clear ();
  setRet.insert (name1);
  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  BOOST_CHECK (setRet == setExpected);

  BOOST_CHECK (!view.GetName (name1, data2));
  view.SetName (name1, dataHeight2, false);
  BOOST_CHECK (view.GetName (name1, data2));
  BOOST_CHECK (dataHeight2 == data2);

  BOOST_CHECK (view.GetNamesForHeight (height1, setRet));
  BOOST_CHECK (setRet == setExpected);
  setExpected.insert (name1);
  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  BOOST_CHECK (setRet == setExpected);

  BOOST_CHECK (view.Flush ());
  BOOST_CHECK (view.GetName (name1, data2));
  BOOST_CHECK (dataHeight2 == data2);

  view.SetName (name2, dataHeight2, false);
  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  setExpected.insert (name2);
  BOOST_CHECK (setRet == setExpected);

  view.DeleteName (name1);
  BOOST_CHECK (!view.GetName (name1, data2));
  BOOST_CHECK (view.Flush ());
  BOOST_CHECK (!view.GetName (name1, data2));

  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  setExpected.erase (name1);
  BOOST_CHECK (setRet == setExpected);

  view.SetName (name2, dataHeight1, false);
  BOOST_CHECK (view.Flush ());
  view.SetName (name1, dataHeight1, false);

  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  setExpected.clear ();
  BOOST_CHECK (setRet == setExpected);
  BOOST_CHECK (view.GetNamesForHeight (height1, setRet));
  setExpected.insert (name1);
  setExpected.insert (name2);
  BOOST_CHECK (setRet == setExpected);

  /* Test name walking.  */

  CTestNameWalker walker;
  walker.single = false;
  view.Flush ();
  view.WalkNames (valtype (), walker);
  setExpected.clear ();
  setExpected.insert (name1);
  setExpected.insert (name2);
  BOOST_CHECK (walker.names == setExpected);

  walker.names.clear ();
  view.WalkNames (name2, walker);
  setExpected.erase (name1);
  BOOST_CHECK (walker.names == setExpected);

  walker.names.clear ();
  walker.single = true;
  view.WalkNames (valtype (), walker);
  setExpected.clear ();
  setExpected.insert (name1);
  BOOST_CHECK (walker.names == setExpected);
}

/* ************************************************************************** */

/**
 * Construct a dummy tx that provides the given script as input
 * for further tests in the given CCoinsView.  The txid is returned
 * to refer to it.  The "index" is always 0.  The output's
 * value is always set to 1000 COIN, since it doesn't matter for
 * the tests we are interested in.
 * @param scr The script that should be provided as output.
 * @param nHeight The height of the coin.
 * @param view Add it to this view.
 * @return The txid.
 */
static uint256
addTestCoin (const CScript& scr, unsigned nHeight, CCoinsViewCache& view)
{
  CMutableTransaction mtx;
  mtx.vout.push_back (CTxOut (1000 * COIN, scr));
  const CTransaction tx(mtx);

  CCoinsModifier entry = view.ModifyCoins (tx.GetHash ());
  *entry = CCoins (tx, nHeight);

  return tx.GetHash ();
}

BOOST_AUTO_TEST_CASE (name_tx_verification)
{
  const valtype name1 = ValtypeFromString ("test-name-1");
  const valtype name2 = ValtypeFromString ("test-name-2");
  const valtype value = ValtypeFromString ("my-value");

  const valtype tooLongName(256, 'x');
  const valtype tooLongValue(1024, 'x');

  const CScript addr = getTestAddress ();

  const valtype rand(20, 'x');
  valtype toHash(rand);
  toHash.insert (toHash.end (), name1.begin (), name1.end ());
  const uint160 hash = Hash160 (toHash);

  /* We use a basic coin view as standard situation for all the tests.
     Set it up with some basic input coins.  */

  CCoinsView dummyView;
  CCoinsViewCache view(&dummyView);

  const CScript scrNew = CNameScript::buildNameNew (addr, hash);
  const CScript scrFirst = CNameScript::buildNameFirstupdate (addr, name1,
                                                              value, rand);
  const CScript scrUpdate = CNameScript::buildNameUpdate (addr, name1, value);

  const uint256 inCoin = addTestCoin (addr, 1, view);
  const uint256 inNew = addTestCoin (scrNew, 100000, view);
  const uint256 inFirst = addTestCoin (scrFirst, 100000, view);
  const uint256 inUpdate = addTestCoin (scrUpdate, 100000, view);

  CNameData data1;
  data1.fromScript (100000, COutPoint (inFirst, 0), CNameScript (scrFirst));
  view.SetName (name1, data1, false);

  /* ****************************************************** */
  /* Try out the Namecoin / non-Namecoin tx version check.  */

  CValidationState state;
  CMutableTransaction mtx;
  CScript scr;

  mtx.vin.push_back (CTxIn (COutPoint (inCoin, 0)));
  mtx.vout.push_back (CTxOut (COIN, addr));
  const CTransaction baseTx(mtx);

  /* Non-name tx should be non-Namecoin version.  */
  BOOST_CHECK (CheckNameTransaction (baseTx, 200000, view, state));
  mtx.SetNamecoin ();
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state));

  /* Name tx should be Namecoin version.  */
  mtx = CMutableTransaction (baseTx);
  mtx.vin.push_back (CTxIn (COutPoint (inNew, 0)));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state));
  mtx.SetNamecoin ();
  mtx.vin.push_back (CTxIn (COutPoint (inUpdate, 0)));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state));

  /* Duplicate name outs are not allowed.  */
  mtx = CMutableTransaction (baseTx);
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state));
  mtx.SetNamecoin ();
  BOOST_CHECK (CheckNameTransaction (mtx, 200000, view, state));
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state));

  /* ************************** */
  /* Test NAME_NEW validation.  */

  /* Basic verification of NAME_NEW.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin ();
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  BOOST_CHECK (CheckNameTransaction (mtx, 200000, view, state));
  mtx.vin.push_back (CTxIn (COutPoint (inNew, 0)));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state));

  /* Greedy names.  */
  mtx.vin.clear ();
  mtx.vout[1].nValue = NAME_LOCKED_AMOUNT;
  BOOST_CHECK (CheckNameTransaction (mtx, 200000, view, state));
  BOOST_CHECK (CheckNameTransaction (mtx, MEMPOOL_HEIGHT, view, state));
  mtx.vout[1].nValue = NAME_LOCKED_AMOUNT - 1;
  BOOST_CHECK (CheckNameTransaction (mtx, 200000, view, state));
  BOOST_CHECK (!CheckNameTransaction (mtx, MEMPOOL_HEIGHT, view, state));

  /* ***************************** */
  /* Test NAME_UPDATE validation.  */

  /* Check update of UPDATE output, plus expiry.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin ();
  mtx.vout.push_back (CTxOut (COIN, scrUpdate));
  BOOST_CHECK (!CheckNameTransaction (mtx, 135999, view, state));
  mtx.vin.push_back (CTxIn (COutPoint (inUpdate, 0)));
  BOOST_CHECK (CheckNameTransaction (mtx, 135999, view, state));
  BOOST_CHECK (!CheckNameTransaction (mtx, 136000, view, state));

  /* Check update of FIRSTUPDATE output, plus expiry.  */
  mtx.vin.clear ();
  mtx.vin.push_back (CTxIn (COutPoint (inFirst, 0)));
  BOOST_CHECK (CheckNameTransaction (mtx, 135999, view, state));
  BOOST_CHECK (!CheckNameTransaction (mtx, 136000, view, state));

  /* Check for "greedy" names conditions.  */
  mtx.vout[1].nValue = NAME_LOCKED_AMOUNT;
  BOOST_CHECK (CheckNameTransaction (mtx, 110000, view, state));
  BOOST_CHECK (CheckNameTransaction (mtx, MEMPOOL_HEIGHT, view, state));
  mtx.vout[1].nValue = NAME_LOCKED_AMOUNT - 1;
  BOOST_CHECK (CheckNameTransaction (mtx, 110000, view, state));
  BOOST_CHECK (!CheckNameTransaction (mtx, MEMPOOL_HEIGHT, view, state));

  /* Value length limits.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin ();
  mtx.vin.push_back (CTxIn (COutPoint (inUpdate, 0)));
  scr = CNameScript::buildNameUpdate (addr, name1, tooLongValue);
  mtx.vout.push_back (CTxOut (COIN, scr));
  BOOST_CHECK (!CheckNameTransaction (mtx, 110000, view, state));
  
  /* Name mismatch to prev out.  */
  mtx.vout.clear ();
  scr = CNameScript::buildNameUpdate (addr, name2, value);
  mtx.vout.push_back (CTxOut (COIN, scr));
  BOOST_CHECK (!CheckNameTransaction (mtx, 110000, view, state));

  /* Previous NAME_NEW is not allowed!  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin ();
  mtx.vout.push_back (CTxOut (COIN, scrUpdate));
  mtx.vin.push_back (CTxIn (COutPoint (inNew, 0)));
  BOOST_CHECK (!CheckNameTransaction (mtx, 110000, view, state));

  /* ********************************** */
  /* Test NAME_FIRSTUPDATE validation.  */

  CCoinsViewCache viewClean(view);
  viewClean.DeleteName (name1);

  /* Basic valid transaction.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin ();
  mtx.vout.push_back (CTxOut (COIN, scrFirst));
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state));
  mtx.vin.push_back (CTxIn (COutPoint (inNew, 0)));
  BOOST_CHECK (CheckNameTransaction (mtx, 100012, viewClean, state));

  /* Maturity of prev out, acceptable for mempool.  */
  BOOST_CHECK (!CheckNameTransaction (mtx, 100011, viewClean, state));
  BOOST_CHECK (CheckNameTransaction (mtx, MEMPOOL_HEIGHT, viewClean, state));

  /* Expiry and re-registration of a name.  */
  BOOST_CHECK (!CheckNameTransaction (mtx, 135999, view, state));
  BOOST_CHECK (CheckNameTransaction (mtx, 136000, view, state));

  /* "Greedy" names.  */
  mtx.vout[1].nValue = NAME_LOCKED_AMOUNT;
  BOOST_CHECK (CheckNameTransaction (mtx, 100012, viewClean, state));
  BOOST_CHECK (CheckNameTransaction (mtx, MEMPOOL_HEIGHT, viewClean, state));
  mtx.vout[1].nValue = NAME_LOCKED_AMOUNT - 1;
  BOOST_CHECK (CheckNameTransaction (mtx, 100012, viewClean, state));
  BOOST_CHECK (!CheckNameTransaction (mtx, MEMPOOL_HEIGHT, viewClean, state));

  /* Rand mismatch (wrong name activated).  */
  mtx.vout.clear ();
  scr = CNameScript::buildNameFirstupdate (addr, name2, value, rand);
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state));

  /* Non-NAME_NEW prev output.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin ();
  mtx.vout.push_back (CTxOut (COIN, scrFirst));
  mtx.vin.push_back (CTxIn (COutPoint (inUpdate, 0)));
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state));
  mtx.vin.clear ();
  mtx.vin.push_back (CTxIn (COutPoint (inFirst, 0)));
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state));
}

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE (name_updates_undo)
{
  /* Enable name history to test this on the go.  */
  fNameHistory = true;

  const valtype name = ValtypeFromString ("database-test-name");
  const valtype value1 = ValtypeFromString ("old-value");
  const valtype value2 = ValtypeFromString ("new-value");
  const CScript addr = getTestAddress ();

  CCoinsView dummyView;
  CCoinsViewCache view(&dummyView);
  CBlockUndo undo;
  CNameData data;
  CNameHistory history;

  const valtype rand(20, 'x');
  valtype toHash(rand);
  toHash.insert (toHash.end (), name.begin (), name.end ());
  const uint160 hash = Hash160 (toHash);

  const CScript scrNew = CNameScript::buildNameNew (addr, hash);
  const CScript scrFirst = CNameScript::buildNameFirstupdate (addr, name,
                                                              value1, rand);
  const CScript scrUpdate = CNameScript::buildNameUpdate (addr, name, value2);

  /* The constructed tx needs not be valid.  We only test
     ApplyNameTransaction and not validation.  */

  CMutableTransaction mtx;
  mtx.SetNamecoin ();
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  ApplyNameTransaction (mtx, 100, view, undo);
  BOOST_CHECK (!view.GetName (name, data));
  BOOST_CHECK (undo.vnameundo.empty ());
  BOOST_CHECK (!view.GetNameHistory (name, history));

  mtx.vout.clear ();
  mtx.vout.push_back (CTxOut (COIN, scrFirst));
  ApplyNameTransaction (mtx, 200, view, undo);
  BOOST_CHECK (view.GetName (name, data));
  BOOST_CHECK (data.getHeight () == 200);
  BOOST_CHECK (data.getValue () == value1);
  BOOST_CHECK (data.getAddress () == addr);
  BOOST_CHECK (!view.GetNameHistory (name, history));
  BOOST_CHECK (undo.vnameundo.size () == 1);
  const CNameData firstData = data;

  mtx.vout.clear ();
  mtx.vout.push_back (CTxOut (COIN, scrUpdate));
  ApplyNameTransaction (mtx, 300, view, undo);
  BOOST_CHECK (view.GetName (name, data));
  BOOST_CHECK (data.getHeight () == 300);
  BOOST_CHECK (data.getValue () == value2);
  BOOST_CHECK (data.getAddress () == addr);
  BOOST_CHECK (view.GetNameHistory (name, history));
  BOOST_CHECK (history.getData ().size () == 1);
  BOOST_CHECK (history.getData ().back () == firstData);
  BOOST_CHECK (undo.vnameundo.size () == 2);

  undo.vnameundo.back ().apply (view);
  BOOST_CHECK (view.GetName (name, data));
  BOOST_CHECK (data.getHeight () == 200);
  BOOST_CHECK (data.getValue () == value1);
  BOOST_CHECK (data.getAddress () == addr);
  BOOST_CHECK (!view.GetNameHistory (name, history) || history.empty ());
  undo.vnameundo.pop_back ();

  undo.vnameundo.back ().apply (view);
  BOOST_CHECK (!view.GetName (name, data));
  BOOST_CHECK (!view.GetNameHistory (name, history) || history.empty ());
  undo.vnameundo.pop_back ();
  BOOST_CHECK (undo.vnameundo.empty ());
}

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE (name_expire_utxo)
{
  const valtype name1 = ValtypeFromString ("test-name-1");
  const valtype name2 = ValtypeFromString ("test-name-2");
  const valtype value = ValtypeFromString ("value");
  const CScript addr = getTestAddress ();
  
  const CScript upd1 = CNameScript::buildNameUpdate (addr, name1, value);
  const CScript upd2 = CNameScript::buildNameUpdate (addr, name2, value);

  const CNameScript op1(upd1);
  const CNameScript op2(upd2);

  /* Use a "real" backing view, since GetNamesForHeight calls through
     to the base in any case.  */
  CCoinsViewCache view(pcoinsTip);

  const uint256 coinId1 = addTestCoin (upd1, 100000, view);
  const uint256 coinId2 = addTestCoin (upd2, 100010, view);

  CNameData data;
  data.fromScript (100000, COutPoint (coinId1, 0), op1);
  view.SetName (name1, data, false);
  BOOST_CHECK (!data.isExpired (135999) && data.isExpired (136000));
  data.fromScript (100010, COutPoint (coinId2, 0), op2);
  view.SetName (name2, data, false);
  BOOST_CHECK (!data.isExpired (136009) && data.isExpired (136010));

  std::set<valtype> setExpired;
  BOOST_CHECK (view.GetNamesForHeight (100000, setExpired));
  BOOST_CHECK (setExpired.size () == 1 && *setExpired.begin () == name1);
  BOOST_CHECK (view.GetNamesForHeight (100010, setExpired));
  BOOST_CHECK (setExpired.size () == 1 && *setExpired.begin () == name2);

  CCoins coin1, coin2;
  BOOST_CHECK (view.GetCoins (coinId1, coin1));
  BOOST_CHECK (view.GetCoins (coinId2, coin2));

  CBlockUndo undo1, undo2;
  CCoins coins;

  /* None of the two names should be expired.  */
  BOOST_CHECK (ExpireNames (135999, view, undo1, setExpired));
  BOOST_CHECK (undo1.vexpired.empty ());
  BOOST_CHECK (setExpired.empty ());
  BOOST_CHECK (view.GetCoins (coinId1, coins));
  BOOST_CHECK (coins == coin1);
  BOOST_CHECK (view.GetCoins (coinId2, coins));
  BOOST_CHECK (coins == coin2);

  /* The first name expires.  */
  BOOST_CHECK (ExpireNames (136000, view, undo1, setExpired));
  BOOST_CHECK (undo1.vexpired.size () == 1);
  BOOST_CHECK (undo1.vexpired[0].txout == coin1.vout[0]);
  BOOST_CHECK (setExpired.size () == 1 && *setExpired.begin () == name1);
  BOOST_CHECK (!view.GetCoins (coinId1, coins));
  BOOST_CHECK (view.GetCoins (coinId2, coins));
  BOOST_CHECK (coins == coin2);

  /* Also the second name expires.  */
  BOOST_CHECK (ExpireNames (136010, view, undo2, setExpired));
  BOOST_CHECK (undo2.vexpired.size () == 1);
  BOOST_CHECK (undo2.vexpired[0].txout == coin2.vout[0]);
  BOOST_CHECK (setExpired.size () == 1 && *setExpired.begin () == name2);
  BOOST_CHECK (!view.GetCoins (coinId1, coins));
  BOOST_CHECK (!view.GetCoins (coinId2, coins));

  /* Undo the second expiration.  */
  BOOST_CHECK (UnexpireNames (136010, undo2, view, setExpired));
  BOOST_CHECK (setExpired.size () == 1 && *setExpired.begin () == name2);
  BOOST_CHECK (!view.GetCoins (coinId1, coins));
  BOOST_CHECK (view.GetCoins (coinId2, coins));
  BOOST_CHECK (coins == coin2);

  /* Undoing at the wrong height should fail.  */
  BOOST_CHECK (!UnexpireNames (136001, undo1, view, setExpired));
  BOOST_CHECK (!UnexpireNames (135999, undo1, view, setExpired));

  /* Undo the first expiration.  */
  BOOST_CHECK (UnexpireNames (136000, undo1, view, setExpired));
  BOOST_CHECK (setExpired.size () == 1 && *setExpired.begin () == name1);
  BOOST_CHECK (view.GetCoins (coinId1, coins));
  BOOST_CHECK (coins == coin1);
  BOOST_CHECK (view.GetCoins (coinId2, coins));
  BOOST_CHECK (coins == coin2);
}

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE (name_mempool)
{
  mempool.clear ();

  const valtype nameReg = ValtypeFromString ("name-reg");
  const valtype nameUpd = ValtypeFromString ("name-upd");
  const valtype value = ValtypeFromString ("value");
  const valtype valueA = ValtypeFromString ("value-a");
  const valtype valueB = ValtypeFromString ("value-b");
  const CScript addr = getTestAddress ();

  const valtype rand1(20, 'a');
  const valtype rand2(20, 'b');

  const CScript first1
    = CNameScript::buildNameFirstupdate (addr, nameReg, value, rand1);
  const CScript first2
    = CNameScript::buildNameFirstupdate (addr, nameReg, value, rand2);
  const CScript upd1 = CNameScript::buildNameUpdate (addr, nameUpd, valueA);
  const CScript upd2 = CNameScript::buildNameUpdate (addr, nameUpd, valueB);

  /* The constructed tx needs not be valid.  We only test
     the mempool acceptance and not validation.  */

  CMutableTransaction txReg1;
  txReg1.SetNamecoin ();
  txReg1.vout.push_back (CTxOut (COIN, first1));
  CMutableTransaction txReg2;
  txReg2.SetNamecoin ();
  txReg2.vout.push_back (CTxOut (COIN, first2));

  CMutableTransaction txUpd1;
  txUpd1.SetNamecoin ();
  txUpd1.vout.push_back (CTxOut (COIN, upd1));
  CMutableTransaction txUpd2;
  txUpd2.SetNamecoin ();
  txUpd2.vout.push_back (CTxOut (COIN, upd2));

  /* For an empty mempool, all tx should be fine.  */
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.updatesName (nameUpd));
  BOOST_CHECK (mempool.checkNameOps (txReg1) && mempool.checkNameOps (txReg2));
  BOOST_CHECK (mempool.checkNameOps (txUpd1) && mempool.checkNameOps (txUpd2));

  /* Add a name registration.  */
  const CTxMemPoolEntry entryReg(txReg1, 0, 0, 0, 100);
  BOOST_CHECK (entryReg.isNameRegistration () && !entryReg.isNameUpdate ());
  BOOST_CHECK (entryReg.getName () == nameReg);
  mempool.addUnchecked (entryReg.GetTx ().GetHash (), entryReg);
  BOOST_CHECK (mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.updatesName (nameReg));
  BOOST_CHECK (!mempool.checkNameOps (txReg2) && mempool.checkNameOps (txUpd1));

  /* Add a name update.  */
  const CTxMemPoolEntry entryUpd(txUpd1, 0, 0, 0, 100);
  BOOST_CHECK (!entryUpd.isNameRegistration () && entryUpd.isNameUpdate ());
  BOOST_CHECK (entryUpd.getName () == nameUpd);
  mempool.addUnchecked (entryUpd.GetTx ().GetHash (), entryUpd);
  BOOST_CHECK (!mempool.registersName (nameUpd));
  BOOST_CHECK (mempool.updatesName (nameUpd));
  BOOST_CHECK (!mempool.checkNameOps (txUpd2));

  /* Run mempool sanity check.  */
  CCoinsView dummyView;
  CCoinsViewCache view(&dummyView);
  const CNameScript nameOp(upd1);
  CNameData data;
  data.fromScript (100, COutPoint (uint256 (), 0), nameOp);
  view.SetName (nameUpd, data, false);
  mempool.setSanityCheck (true);
  mempool.check (&view);

  /* Remove the transactions again.  */

  std::list<CTransaction> removed;
  mempool.remove (txReg1, removed, true);
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (mempool.checkNameOps (txReg1) && mempool.checkNameOps (txReg2));
  BOOST_CHECK (!mempool.checkNameOps (txUpd2));
  BOOST_CHECK (removed.size () == 1);

  mempool.remove (txUpd1, removed, true);
  BOOST_CHECK (!mempool.updatesName (nameUpd));
  BOOST_CHECK (mempool.checkNameOps (txUpd1) && mempool.checkNameOps (txUpd2));
  BOOST_CHECK (mempool.checkNameOps (txReg1));
  BOOST_CHECK (removed.size () == 2);

  /* Check removing of conflicted name registrations.  */

  mempool.addUnchecked (entryReg.GetTx ().GetHash (), entryReg);
  BOOST_CHECK (mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.checkNameOps (txReg2));

  removed.clear ();
  mempool.removeConflicts (txReg2, removed);
  BOOST_CHECK (removed.size () == 1);
  BOOST_CHECK (removed.front ().GetHash () == txReg1.GetHash ());
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (mempool.mapTx.empty ());

  /* Check removing of conflicts after name expiration.  */

  mempool.addUnchecked (entryUpd.GetTx ().GetHash (), entryUpd);
  BOOST_CHECK (mempool.updatesName (nameUpd));
  BOOST_CHECK (!mempool.checkNameOps (txUpd2));

  std::set<valtype> names;
  names.insert (nameUpd);
  removed.clear ();
  mempool.removeExpireConflicts (names, removed);
  BOOST_CHECK (removed.size () == 1);
  BOOST_CHECK (removed.front ().GetHash () == txUpd1.GetHash ());
  BOOST_CHECK (!mempool.updatesName (nameUpd));
  BOOST_CHECK (mempool.mapTx.empty ());

  /* Check removing of conflicts after name unexpiration.  */

  mempool.addUnchecked (entryReg.GetTx ().GetHash (), entryReg);
  BOOST_CHECK (mempool.registersName (nameReg));
  BOOST_CHECK (!mempool.checkNameOps (txReg2));

  names.clear ();
  names.insert (nameReg);
  removed.clear ();
  mempool.removeUnexpireConflicts (names, removed);
  BOOST_CHECK (removed.size () == 1);
  BOOST_CHECK (removed.front ().GetHash () == txReg1.GetHash ());
  BOOST_CHECK (!mempool.registersName (nameReg));
  BOOST_CHECK (mempool.mapTx.empty ());
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
