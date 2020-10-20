#!/usr/bin/env python3
# Copyright (c) 2014-2019 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# General code for Namecoin tests.

from .test_framework import BitcoinTestFramework

from .util import assert_equal


class NameTestFramework (BitcoinTestFramework):

  def setup_name_test (self, args = [[]] * 4):
    self.num_nodes = len (args)
    self.extra_args = args
    self.node_groups = None

    # Enable mocktime based on the value for the cached blockchain from
    # test_framework.py.  This is needed to get us out of IBD.
    self.mocktime = 1388534400 + (201 * 10 * 60)

  def firstupdateName (self, ind, name, newData, value = None,
                       opt = None, allowActive = False):
    """
    Utility routine to perform a name_firstupdate command.  The rand
    and txid are taken from 'newData', as it is returned by name_new.
    """

    node = self.nodes[ind]

    if allowActive:
      if opt is None:
        opt = {}
      return node.name_firstupdate (name, newData[1], newData[0],
                                    value, opt, True)

    if opt is not None:
      return node.name_firstupdate (name, newData[1], newData[0], value, opt)
    if value is not None:
      return node.name_firstupdate (name, newData[1], newData[0], value)
    return node.name_firstupdate (name, newData[1], newData[0])

  def checkName (self, ind, name, value, expiresIn, expired):
    """
    Query a name with name_show and check that certain data fields
    match the expectations.  Returns the full JSON object.
    """

    data = self.nodes[ind].name_show (name)
    self.checkNameData (data, name, value, expiresIn, expired)

    return data

  def checkNameData (self, data, name, value, expiresIn, expired):
    """
    Check a name info object against expected data.
    """

    assert_equal (data['name'], name)
    assert_equal (data['value'], value)
    if (expiresIn is not None):
      assert_equal (data['expires_in'], expiresIn)
    assert isinstance (data['expired'], bool)
    assert_equal (data['expired'], expired)

  def checkNameHistory (self, ind, name, values):
    """
    Query for the name_history of 'name' and check that its historical
    values, in order of increasing height, are the ones in 'values'.
    """

    data = self.nodes[ind].name_history (name)

    valuesFound = []
    for e in data:
      assert_equal (e['name'], name)
      valuesFound.append (e['value'])

    assert_equal (valuesFound, values)

  def rawtxOutputIndex (self, ind, txhex, addr):
    """
    Returns the index of the tx output in the given raw transaction that
    is sent to the given address.

    This is useful for building raw transactions with namerawtransaction.
    """

    tx = self.nodes[ind].decoderawtransaction (txhex)
    for i, vout in enumerate (tx['vout']):
      if addr in vout['scriptPubKey']['addresses']:
        return i

    return None

  def atomicTrade (self, name, value, price, fee, nameFrom, nameTo):
    """
    Perform an atomic name trade, sending 'name' from the first to the
    second node (referenced by their index).  Also send 'price' (we assume
    that it is less than each unspent output in 'listunspent') the
    other way round.  Returned is the txid.
    """

    addrA = self.nodes[nameFrom].getnewaddress ()
    addrB = self.nodes[nameTo].getnewaddress ()
    addrChange = self.nodes[nameTo].getrawchangeaddress ()

    inputs = []

    unspents = self.nodes[nameTo].listunspent ()
    txin = None
    for u in unspents:
      if u['amount'] >= price + fee:
        txin = u
        break
    assert txin is not None
    change = txin['amount'] - price - fee
    inputs.append ({"txid": txin['txid'], "vout": txin['vout']})

    data = self.nodes[nameFrom].name_show (name)
    nameTxo = self.nodes[nameFrom].gettxout (data['txid'], data['vout'])
    nameAmount = nameTxo['value']

    inputs.append ({"txid": data['txid'], "vout": data['vout']})
    outputs = {addrA: price, addrChange: change, addrB: nameAmount}
    tx = self.nodes[nameFrom].createrawtransaction (inputs, outputs)

    nameInd = self.rawtxOutputIndex (nameFrom, tx, addrB)
    nameOp = {"op": "name_update", "name": name, "value": value}
    tx = self.nodes[nameFrom].namerawtransaction (tx, nameInd, nameOp)

    signed = self.nodes[nameFrom].signrawtransactionwithwallet (tx['hex'])
    assert not signed['complete']
    signed = self.nodes[nameTo].signrawtransactionwithwallet (signed['hex'])
    assert signed['complete']
    tx = signed['hex']
    
    return self.nodes[nameFrom].sendrawtransaction (tx)
