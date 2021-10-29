#!/usr/bin/env python3
# Copyright (c) 2018-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the interaction between names and the UTXO set.

from test_framework.names import NameTestFramework
from test_framework.util import *


class NameUtxoTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 1)

  def run_test (self):
    node = self.nodes[0]

    # Unlike Namecoin which has stale name_new's and expired names,
    # we just need an active name for Xaya.
    node.name_register ("d/active", "{}")
    self.generate (node, 1)

    # The active name should be there.
    data = node.name_show ("d/active")
    txo = node.gettxout (data['txid'], data['vout'])
    nameOp = txo['scriptPubKey']['nameOp']
    assert_equal (nameOp['op'], 'name_register')
    assert_equal (nameOp['name'], 'd/active')

    # Verify the expected result for gettxoutsetinfo.
    data = node.gettxoutsetinfo ()
    height = data['height']
    assert_equal (height, 201)
    assert_equal (data['txouts'], 203)
    amount = data['amount']
    assert_equal (amount['names'], Decimal ('0.01'))
    assert_equal (amount['total'], amount['names'] + amount['coins'])
    mined = 222222222 + 149 * 50 + (height - 149) * 25
    assert_equal (amount['total'], mined)


if __name__ == '__main__':
  NameUtxoTest ().main ()
