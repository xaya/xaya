#!/usr/bin/env python3
# Copyright (c) 2019-2021 The Xaya developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""RPC test for the "burn" option of name operations."""

from test_framework.names import NameTestFramework, val
from test_framework.util import *

import codecs

# Maximum data length.
MAX_DATA_LEN = 80


class XayaCreateBurnsTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([["-debug"]] * 1)

  def verifyTx (self, txid, expected):
    """Verify that the given tx burns coins with the expected data keys."""

    txData = self.nodes[0].gettransaction (txid)
    assert_greater_than (txData["confirmations"], 0)

    tx = self.nodes[0].decoderawtransaction (txData["hex"])
    vout = tx['vout']

    # There should be two additional outputs: name and change
    assert_equal (len (vout), len (expected) + 2)

    actual = {}
    for out in vout:
      if not "burn" in out["scriptPubKey"]:
        continue

      data = codecs.decode (out["scriptPubKey"]["burn"], "hex")
      data = codecs.decode (data, "ascii")

      actual[data] = out["value"]

    assert_equal (actual, expected)

  def run_test (self):

    # Send name_register with burn and verify it worked as expected.
    burn = {"foobar": 1, "baz baz baz": 2}
    txid = self.nodes[0].name_register ("x/testname", val ("value"),
                                        {"burn": burn})
    self.generate (self.nodes[0], 1)
    self.verifyTx (txid, burn)

    # Test different variations (numbers of burns) with name_update.
    for n in range (5):
      burn = {"foo %d" % i: 42 + i for i in range (n)}
      txid = self.nodes[0].name_update ("x/testname", val ("value"),
                                       {"burn": burn})
      self.generate (self.nodes[0], 1)
      self.verifyTx (txid, burn)

    # Test maximum length.
    burn = {"a" * MAX_DATA_LEN: 1, "b" * MAX_DATA_LEN: 2}
    txid = self.nodes[0].name_update ("x/testname", val ("value"),
                                     {"burn": burn})
    self.generate (self.nodes[0], 1)
    self.verifyTx (txid, burn)

    # Verify the range check for amount and the data length verification.
    assert_raises_rpc_error (-3, 'Invalid amount for burn',
                             self.nodes[0].name_update,
                             "x/testname", val ("value"),
                             {"burn": {"foo": 0}})
    assert_raises_rpc_error (-5, 'Burn data is too long',
                             self.nodes[0].name_update,
                             "x/testname", val ("value"),
                             {"burn": {"x" * (MAX_DATA_LEN + 1): 1}})

    # Verify the insufficient funds check, both where it fails a priori
    # and where we just don't have enough for the fee.
    balance = self.nodes[0].getbalance ()
    assert_raises_rpc_error (-6, 'Insufficient funds',
                             self.nodes[0].name_update,
                             "x/testname", val ("value"),
                             {"burn": {"x": balance + 1}})
    assert_raises_rpc_error (-6, 'Insufficient funds',
                             self.nodes[0].name_update,
                             "x/testname", val ("value"),
                             {"burn": {"x": balance}})

    # Check that we can send a name_update that burns almost all funds in
    # the wallet.  We only need to keep a tiny amount to pay for the fee
    # (but less than the locked amount of 0.01).
    keep = Decimal ("0.009")
    burn = {"some burn data": balance - keep}
    txid = self.nodes[0].name_update ("x/testname", val ("value"),
                                      {"burn": burn})
    self.generate (self.nodes[0], 1)
    self.verifyTx (txid, burn)


if __name__ == '__main__':
  XayaCreateBurnsTest ().main ()
