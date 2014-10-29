#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC tests for name operations and the rawtx API.

# Add python-bitcoinrpc to module search path:
import os
import sys
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-bitcoinrpc"))

from bitcoinrpc.authproxy import JSONRPCException
from decimal import Decimal
from names import NameTestFramework
from util import assert_equal

class NameRawTxTest (NameTestFramework):

  def run_test (self):
    NameTestFramework.run_test (self)

    # Decode name_new.
    new = self.nodes[0].name_new ("my-name")
    self.generate (0, 10)
    data = self.decodeNameTx (0, new[0])
    assert_equal (data['op'], "name_new")
    assert 'hash' in data

    # Decode name_firstupdate.
    first = self.firstupdateName (0, "my-name", new, "initial value")
    self.generate (0, 5)
    data = self.decodeNameTx (0, first)
    assert_equal (data['op'], "name_firstupdate")
    assert_equal (data['name'], "my-name")
    assert_equal (data['value'], "initial value")
    assert_equal (data['rand'], new[1])

    # Decode name_update.
    upd = self.nodes[0].name_update ("my-name", "new value")
    self.generate (0, 1)
    data = self.decodeNameTx (0, upd)
    assert_equal (data['op'], "name_update")
    assert_equal (data['name'], "my-name")
    assert_equal (data['value'], "new value")

    # Perform a rawtx name update together with an atomic currency transaction.
    # We send the test name from 0 to 1 and some coins from 1 to 0.  In other
    # words, perform an atomic name trade.

    addrA = self.nodes[0].getnewaddress ()
    addrB = self.nodes[1].getnewaddress ()
    balanceA = self.nodes[0].getbalance ()
    balanceB = self.nodes[1].getbalance ()

    unspents = self.nodes[1].listunspent ()
    assert (len (unspents) > 0)
    txin = unspents[0]

    inputs = [{"txid": txin['txid'], "vout": txin['vout']}]
    outputs = {addrA: txin['amount']}
    nameOp = {"op": "name_update", "name": "my-name",
              "value": "enjoy", "address": addrB}

    tx = self.nodes[2].createrawtransaction (inputs, outputs, nameOp)
    signed = self.nodes[0].signrawtransaction (tx)
    assert not signed['complete']
    signed = self.nodes[1].signrawtransaction (signed['hex'])
    assert signed['complete']
    tx = signed['hex']
    self.nodes[2].sendrawtransaction (tx)
    self.generate (3, 1)

    data = self.checkName (3, "my-name", "enjoy", None, False)
    assert_equal (data['address'], addrB)
    data = self.nodes[0].name_list ("my-name")
    assert_equal (len (data), 1)
    assert_equal (data[0]['name'], "my-name")
    assert_equal (data[0]['transferred'], True)
    data = self.nodes[1].name_list ("my-name")
    assert_equal (len (data), 1)
    assert_equal (data[0]['name'], "my-name")
    assert_equal (data[0]['transferred'], False)

    # Node 0 also got a block matured.  Take this into account.
    # FIXME: Update checks once getbalance no longer includes names.
    assert_equal (balanceA + txin['amount'] + Decimal ("49.99"),
                  self.nodes[0].getbalance ())
    assert_equal (balanceB - txin['amount'] + Decimal ("0.01"),
                  self.nodes[1].getbalance ())

  def decodeNameTx (self, ind, txid):
    """
    Call the node's getrawtransaction on the txid and find the output
    that is a name operation.  Return the decoded nameop entry.
    """

    data = self.nodes[ind].getrawtransaction (txid, 1)
    res = None
    for out in data['vout']:
      if 'nameOp' in out['scriptPubKey']:
        assert res is None
        res = out['scriptPubKey']['nameOp']

        # Extra check:  Verify that the address is decoded correctly.
        addr = out['scriptPubKey']['addresses']
        assert_equal (out['scriptPubKey']['type'], "pubkeyhash")
        assert_equal (len (addr), 1)
        validation = self.nodes[ind].validateaddress (addr[0])
        assert validation['isvalid']

    assert res is not None
    return res

if __name__ == '__main__':
  NameRawTxTest ().main ()
