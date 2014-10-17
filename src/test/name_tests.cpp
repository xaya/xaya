// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "main.h"
#include "names.h"

#include "core/transaction.h"

#include "script/names.h"

#include <boost/test/unit_test.hpp>

#include <stdint.h>

BOOST_AUTO_TEST_SUITE (name_tests)

BOOST_AUTO_TEST_CASE (name_scripts)
{
  CBitcoinAddress addr("N5e1vXUUL3KfhPyVjQZSes1qQ7eyarDbUU");
  BOOST_CHECK (addr.IsValid ());

  const CScript scrAddr = GetScriptForDestination (addr.Get ());
  const CNameScript opNone(scrAddr);
  BOOST_CHECK (!opNone.isNameOp ());
  BOOST_CHECK (opNone.getAddress () == scrAddr);

  const valtype name = ValtypeFromString ("my-cool-name");
  const valtype value = ValtypeFromString ("42!");

  const valtype rand(uint64_t (0x12345678));
  valtype toHash(rand);
  toHash.insert (toHash.end (), name.begin (), name.end ());
  const uint160 hash = Hash160 (toHash);

  CScript script;
  script = CNameScript::buildNameNew (scrAddr, hash);
  const CNameScript opNew(script);
  BOOST_CHECK (opNew.isNameOp ());
  BOOST_CHECK (opNew.getAddress () == scrAddr);
  BOOST_CHECK (!opNew.isAnyUpdate ());
  BOOST_CHECK (opNew.getNameOp () == OP_NAME_NEW);
  BOOST_CHECK (uint160 (opNew.getOpHash ()) == hash);

  script = CNameScript::buildNameFirstupdate (scrAddr, name, value, rand);
  const CNameScript opFirstupdate(script);
  BOOST_CHECK (opFirstupdate.isNameOp ());
  BOOST_CHECK (opFirstupdate.getAddress () == scrAddr);
  BOOST_CHECK (opFirstupdate.isAnyUpdate ());
  BOOST_CHECK (opFirstupdate.getNameOp () == OP_NAME_FIRSTUPDATE);
  BOOST_CHECK (opFirstupdate.getOpName () == name);
  BOOST_CHECK (opFirstupdate.getOpValue () == value);
  BOOST_CHECK (opFirstupdate.getOpRand () == rand);

  script = CNameScript::buildNameUpdate (scrAddr, name, value);
  const CNameScript opUpdate(script);
  BOOST_CHECK (opUpdate.isNameOp ());
  BOOST_CHECK (opUpdate.getAddress () == scrAddr);
  BOOST_CHECK (opUpdate.isAnyUpdate ());
  BOOST_CHECK (opUpdate.getNameOp () == OP_NAME_UPDATE);
  BOOST_CHECK (opUpdate.getOpName () == name);
  BOOST_CHECK (opUpdate.getOpValue () == value);
}

BOOST_AUTO_TEST_CASE (name_database)
{
  const valtype name = ValtypeFromString ("database-test-name");
  const valtype value = ValtypeFromString ("my-value");

  CBitcoinAddress addr("N5e1vXUUL3KfhPyVjQZSes1qQ7eyarDbUU");
  BOOST_CHECK (addr.IsValid ());
  const CScript scrAddr = GetScriptForDestination (addr.Get ());

  CNameData data, data2;
  CScript updateScript = CNameScript::buildNameUpdate (scrAddr, name, value);
  const CNameScript nameOp(updateScript);
  data.fromScript (42, nameOp);

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

BOOST_AUTO_TEST_SUITE_END ()
