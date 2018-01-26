#!/usr/bin/env python3
# Copyright (c) 2014-2018 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# General code for Chimaera tests.

from .test_framework import BitcoinTestFramework
from .util import *

class NameTestFramework (BitcoinTestFramework):

  def setup_name_test (self, args = [[]] * 4):
    self.num_nodes = len (args)
    self.extra_args = args
    self.node_groups = None

    # Since we use a cached chain, enable mocktime so nodes do not see
    # themselves in IBD.
    self.enable_mocktime ()

  def split_network (self):
    # Override this method to keep track of the node groups, so that we can
    # sync_with_mode correctly.
    super ().split_network ()
    self.node_groups = [self.nodes[:2], self.nodes[2:]]

  def join_network (self):
    super ().join_network ()
    self.node_groups = None

  def sync_with_mode (self, mode = 'both'):
    modes = {'both': {'blocks': True, 'mempool': True},
             'blocks': {'blocks': True, 'mempool': False},
             'mempool': {'blocks': False, 'mempool': True}}
    assert mode in modes

    node_groups = self.node_groups
    if not node_groups:
        node_groups = [self.nodes]

    if modes[mode]['blocks']:
        [sync_blocks(group) for group in node_groups]
    if modes[mode]['mempool']:
        [sync_mempools(group) for group in node_groups]

  def generate (self, ind, blocks, syncBefore = True):
    """
    Generate blocks and sync all nodes.
    """

    # Sync before to get the mempools up-to-date and sync afterwards
    # to ensure that all blocks have propagated.

    if syncBefore:
        self.sync_with_mode ('both')
    self.nodes[ind].generate (blocks)
    self.sync_with_mode ('blocks')

  def checkName (self, ind, name, value):
    """
    Query a name with name_show and check that certain data fields
    match the expectations.  Returns the full JSON object.
    """

    data = self.nodes[ind].name_show (name)
    self.checkNameData (data, name, value)

    return data

  def checkNameData (self, data, name, value):
    """
    Check a name info object against expected data.
    """

    assert_equal (data['name'], name)
    assert_equal (data['value'], value)

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

    signed = self.nodes[nameFrom].signrawtransaction (tx['hex'])
    assert not signed['complete']
    signed = self.nodes[nameTo].signrawtransaction (signed['hex'])
    assert signed['complete']
    tx = signed['hex']
    
    return self.nodes[nameFrom].sendrawtransaction (tx)
