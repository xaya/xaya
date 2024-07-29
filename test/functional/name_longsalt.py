#!/usr/bin/env python3
# Copyright (c) 2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for the "long-salt" rule introduced with the Taproot softfork.

from test_framework.names import NameTestFramework

from test_framework.util import (
  assert_equal,
  assert_raises_rpc_error,
)


class NameLongSaltTest (NameTestFramework):

  def set_test_params (self):
    # Node 0 has Taproot inactive, node 1 has it active (and the long-salt
    # change is tied in to Taproot activation).
    self.setup_name_test ([["-vbparams=taproot:1:1"], []])

  def findUnspent (self, node, amount):
    """
    Finds and returns an unspent input the node has with at least the
    given value.
    """

    unspents = node.listunspent ()
    for u in unspents:
      if u['amount'] >= amount:
        return u

    raise AssertionError ("Could not find suitable unspent output")

  def sendNameNew (self, node, nm, salt):
    """
    Sends a name_new transaction for the given name and salt, using
    raw-transaction capabilities (so that we can actually do that with
    an arbitrary chosen salt).  Returns the outpoint JSON of the
    generated UTXO.
    """

    inp = [self.findUnspent (node, 1)]
    out = [{node.getnewaddress (): 0.01}]
    tx = node.createrawtransaction (inp, out)

    nameOp = {
      "op": "name_new",
      "name": nm,
      "rand": salt.hex (),
    }
    tx = node.namerawtransaction (tx, 0, nameOp)["hex"]

    signed = node.signrawtransactionwithwallet (tx)
    assert_equal (signed["complete"], True)
    txid = node.sendrawtransaction (signed["hex"], 0)
    
    return {"txid": txid, "vout": 0}

  def generateFirstupdate (self, node, nm, value, salt, newInp):
    """
    Generate a raw name_firstupdate transaction for the given data, without
    attempting to broadcast or mine it.  Returned is the transaction as hex
    string.
    """

    inp = [newInp, self.findUnspent (node, 1)]
    out = [{node.getnewaddress (): 0.01}]
    tx = node.createrawtransaction (inp, out)

    nameOp = {
      "op": "name_firstupdate",
      "name": nm,
      "value": value,
      "rand": salt.hex (),
    }
    tx = node.namerawtransaction (tx, 0, nameOp)["hex"]

    signed = node.signrawtransactionwithwallet (tx)
    assert_equal (signed["complete"], True)

    return signed["hex"]

  def run_test (self):
    self.generate (self.nodes[0], 100)

    nm = "x/foo"
    val = "{}"
    salt = b"x" * 19
    new = self.sendNameNew (self.nodes[0], nm, salt)
    self.generate (self.nodes[0], 20)

    firstUpd = self.generateFirstupdate (self.nodes[0], nm, val, salt, new)

    # Both nodes won't accept the transaction by policy.
    for n in self.nodes:
      assert_raises_rpc_error (-26, "rand value is too short",
                               n.sendrawtransaction, firstUpd, 0)

    # In a block, this should be rejected by taproot but accepted
    # without.
    addr = self.nodes[0].getnewaddress ()
    res = self.generateblock (self.nodes[0], addr, [firstUpd],
                              sync_fun=self.no_op)
    blk = self.nodes[0].getblock (res["hash"], 0)
    # Make sure node 1 knows about the block (but it will not accept it).
    self.nodes[1].submitblock (blk)
    assert_equal (self.nodes[0].getbestblockhash (), res["hash"])
    assert_equal (self.nodes[1].getblockcount () + 1,
                  self.nodes[0].getblockcount ())


if __name__ == '__main__':
  NameLongSaltTest (__file__).main ()
