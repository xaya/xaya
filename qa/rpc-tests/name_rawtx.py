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
from names import NameTestFramework
from util import assert_equal

class NameRawTxTest (NameTestFramework):

  def run_test (self, nodes):
    # TODO: call super class

    # Decode name_new.
    new = nodes[0].name_new ("my-name")
    self.generate (nodes, 0, 10)
    data = self.decodeNameTx (nodes[0], new[0])
    assert_equal (data['op'], "name_new")
    assert 'hash' in data

    # Decode name_firstupdate.
    first = self.firstupdateName (nodes[0], "my-name", new, "initial value")
    self.generate (nodes, 0, 5)
    data = self.decodeNameTx (nodes[0], first)
    assert_equal (data['op'], "name_firstupdate")
    assert_equal (data['name'], "my-name")
    assert_equal (data['value'], "initial value")
    assert_equal (data['rand'], new[1])

    # Decode name_update.
    upd = nodes[0].name_update ("my-name", "new value")
    self.generate (nodes, 0, 1)
    data = self.decodeNameTx (nodes[0], upd)
    assert_equal (data['op'], "name_update")
    assert_equal (data['name'], "my-name")
    assert_equal (data['value'], "new value")

    # TODO: test raw tx creation

  def decodeNameTx (self, node, txid):
    """
    Call the node's getrawtransaction on the txid and find the output
    that is a name operation.  Return the decoded nameop entry.
    """

    data = node.getrawtransaction (txid, 1)
    res = None
    for out in data['vout']:
      if 'nameOp' in out['scriptPubKey']:
        assert res is None
        res = out['scriptPubKey']['nameOp']

        # Extra check:  Verify that the address is decoded correctly.
        addr = out['scriptPubKey']['addresses']
        assert_equal (out['scriptPubKey']['type'], "pubkeyhash")
        assert_equal (len (addr), 1)
        validation = node.validateaddress (addr[0])
        assert validation['isvalid']

    assert res is not None
    return res

if __name__ == '__main__':
  NameRawTxTest ().main ()
