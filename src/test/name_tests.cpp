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

BOOST_AUTO_TEST_CASE (name_database)
{
  const valtype name = ValtypeFromString ("database-test-name");
  const valtype value = ValtypeFromString ("my-value");
  const CScript addr = getTestAddress ();

  CNameData data, data2;
  CScript updateScript = CNameScript::buildNameUpdate (addr, name, value);
  const CNameScript nameOp(updateScript);
  data.fromScript (42, uint256 (), nameOp);

  CCoinsViewCache& view = *pcoinsTip;

  BOOST_CHECK (!view.GetName (name, data2));
  view.SetName (name, data);
  BOOST_CHECK (view.GetName (name, data2));
  BOOST_CHECK (data == data2);
  BOOST_CHECK (view.Flush ());
  BOOST_CHECK (view.GetName (name, data2));
  BOOST_CHECK (data == data2);

  view.DeleteName (name);
  BOOST_CHECK (!view.GetName (name, data2));
  BOOST_CHECK (view.Flush ());
  BOOST_CHECK (!view.GetName (name, data2));
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
  data1.fromScript (100000, inFirst, CNameScript (scrFirst));
  view.SetName (name1, data1);

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

  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin ();
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  BOOST_CHECK (CheckNameTransaction (mtx, 200000, view, state));
  mtx.vin.push_back (CTxIn (COutPoint (inNew, 0)));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state));

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
  const valtype name = ValtypeFromString ("database-test-name");
  const valtype value1 = ValtypeFromString ("old-value");
  const valtype value2 = ValtypeFromString ("new-value");
  const CScript addr = getTestAddress ();

  CCoinsView dummyView;
  CCoinsViewCache view(&dummyView);
  CBlockUndo undo;
  CNameData data;

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

  mtx.vout.clear ();
  mtx.vout.push_back (CTxOut (COIN, scrFirst));
  ApplyNameTransaction (mtx, 200, view, undo);
  BOOST_CHECK (view.GetName (name, data));
  BOOST_CHECK (data.getHeight () == 200);
  BOOST_CHECK (data.getValue () == value1);
  BOOST_CHECK (data.getAddress () == addr);
  BOOST_CHECK (undo.vnameundo.size () == 1);

  mtx.vout.clear ();
  mtx.vout.push_back (CTxOut (COIN, scrUpdate));
  ApplyNameTransaction (mtx, 300, view, undo);
  BOOST_CHECK (view.GetName (name, data));
  BOOST_CHECK (data.getHeight () == 300);
  BOOST_CHECK (data.getValue () == value2);
  BOOST_CHECK (data.getAddress () == addr);
  BOOST_CHECK (undo.vnameundo.size () == 2);

  undo.vnameundo.back ().apply (view);
  BOOST_CHECK (view.GetName (name, data));
  BOOST_CHECK (data.getHeight () == 200);
  BOOST_CHECK (data.getValue () == value1);
  BOOST_CHECK (data.getAddress () == addr);
  undo.vnameundo.pop_back ();

  undo.vnameundo.back ().apply (view);
  BOOST_CHECK (!view.GetName (name, data));
  undo.vnameundo.pop_back ();
  BOOST_CHECK (undo.vnameundo.empty ());
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
