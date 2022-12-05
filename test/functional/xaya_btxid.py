#!/usr/bin/env python3
# Copyright (c) 2020-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Tests basic properties of the bare hash (btxid)."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.names import val
from test_framework.util import (
  assert_equal,
  assert_greater_than,
)

from decimal import Decimal


class BtxidTest (BitcoinTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.num_nodes = 2

  def skip_test_if_missing_module (self):
    self.skip_if_no_wallet ()

  def add_options (self, parser):
    self.add_wallet_options (parser)

  def setup_nodes (self):
    # One of our nodes is using legacy (non-segwit) addresses, and the other
    # is using segwit.  This way, the test transaction will have both segwit
    # and non-segwit inputs.
    args = [
      ["-addresstype=legacy"],
      ["-addresstype=bech32"],
    ]
    self.add_nodes (self.num_nodes, extra_args=args)
    self.start_nodes ()

  def build_tx (self, utxo, chiOut, name, nameAddr, value):
    """
    Builds and returns (in the form returned by decoderawtransaction)
    a transaction that spends the given utxo, pays CHI to some output
    (as dict {addr: value}) and updates/transfers the name as given.

    The transaction is just constructed but not signed at all.
    """

    nameData = self.nodes[0].name_show (name)
    inputs = [nameData, utxo]
    outputs = {nameAddr: Decimal ('0.01')}
    outputs.update (chiOut)

    tx = self.nodes[0].createrawtransaction (inputs, outputs)
    nameOp = {
      "op": "name_update",
      "name": name,
      "value": value,
    }
    tx = self.nodes[0].namerawtransaction (tx, 0, nameOp)

    res = self.nodes[0].decoderawtransaction (tx["hex"])
    res["hex"] = tx["hex"]

    return res

  def sign (self, node, tx):
    """
    Signs a transaction (in format of build_tx) with the given node,
    and returns the decoderawtransaction-type result again.
    """

    signed = node.signrawtransactionwithwallet (tx["hex"])

    res = node.decoderawtransaction (signed["hex"])
    res.update (signed)

    return res

  def run_test (self):
    self.nodes[0].createwallet ("")
    self.nodes[1].createwallet ("")
    addr1 = self.nodes[0].getnewaddress ()
    self.generatetoaddress (self.nodes[0], 20, addr1)
    self.sync_blocks ()
    addr2 = self.nodes[1].getnewaddress ()
    self.generatetoaddress (self.nodes[1], 120, addr2)
    self.sync_blocks ()

    # We perform an atomic name trade between our two nodes, and make sure
    # that the btxid changes with important tx details but not when the
    # parties sign the transaction.

    self.nodes[0].name_register ("p/test", val ("0"))
    self.generate (self.nodes[0], 1)
    data = self.nodes[0].name_show ("p/test")
    assert_equal (data["address"][0], "c")
    assert not data["address"].startswith ("chirt")

    utxos = self.nodes[1].listunspent ()[:2]
    value = None
    for u in utxos:
      assert u["address"].startswith ("chirt")
      if value is None or value > u["amount"]:
        value = u["amount"]
    value -= 1
    assert_greater_than (value, 10)

    addr1 = self.nodes[0].getnewaddress ()
    addr1p = self.nodes[0].getnewaddress ()
    addr2 = self.nodes[1].getnewaddress ()

    unsigned = self.build_tx (utxos[0], {addr1: value},
                              "p/test", addr2, val ("1"))

    changed = self.build_tx (utxos[1], {addr1: value},
                             "p/test", addr2, val ("1"))
    assert changed["btxid"] != unsigned["btxid"]

    changed = self.build_tx (utxos[0], {addr1p: value},
                             "p/test", addr2, val ("1"))
    assert changed["btxid"] != unsigned["btxid"]

    changed = self.build_tx (utxos[0], {addr1: value - 1},
                             "p/test", addr2, val ("1"))
    assert changed["btxid"] != unsigned["btxid"]

    changed = self.build_tx (utxos[0], {addr1: value},
                             "p/test", addr2, val ("2"))
    assert changed["btxid"] != unsigned["btxid"]

    partial1 = self.sign (self.nodes[0], unsigned)
    assert_equal (partial1["complete"], False)
    assert_equal (partial1["btxid"], unsigned["btxid"])
    assert partial1["txid"] != unsigned["txid"]
    assert partial1["hash"] != unsigned["hash"]

    partial2 = self.sign (self.nodes[1], unsigned)
    assert_equal (partial2["complete"], False)
    assert_equal (partial2["btxid"], unsigned["btxid"])
    assert_equal (partial2["txid"], unsigned["txid"])
    assert partial2["hash"] != unsigned["hash"]

    full = self.sign (self.nodes[1], partial1)
    assert_equal (full["complete"], True)
    assert_equal (full["btxid"], unsigned["btxid"])
    assert full["txid"] != unsigned["txid"]
    assert full["hash"] != unsigned["hash"]


if __name__ == '__main__':
  BtxidTest ().main ()
