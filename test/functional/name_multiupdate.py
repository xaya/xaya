#!/usr/bin/env python3
# Copyright (c) 2019 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the behaviour of multiple updates for one name within a single block.

from test_framework.names import NameTestFramework, val

from test_framework.util import (
  assert_equal,
  assert_raises_rpc_error,
)


class NameMultiUpdateTest (NameTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.setup_name_test ([["-debug", "-namehistory"]] * 1)

  def run_test (self):
    self.node = self.nodes[0]
    self.node.generate (110)

    # Register a test name.
    name = "d/test"
    new = self.node.name_register (name, val ("first"))

    # Building an update on top of a pending registration.
    self.node.name_update (name, val ("second"))

    # Finalise the registration.
    self.node.generate (1)
    self.checkName (0, name, val ("second"))
    assert_equal (self.node.name_pending (), [])

    # Build two update transactions building on each other.
    txn = []
    txn.append (self.node.name_update (name, val ("third")))
    txn.append (self.node.name_update (name, val ("fourth")))

    # Check that both are in the mempool.
    assert_equal (set (self.node.getrawmempool ()), set (txn))
    pending = self.node.name_pending (name)
    pendingNameVal = [(p["name"], p["value"]) for p in pending]
    assert_equal (pendingNameVal,
                  [(name, val ("third")), (name, val ("fourth"))])

    # Mine transactions and verify the effect.
    self.node.generate (1)
    self.checkName (0, name, val ("fourth"))
    values = [h["value"] for h in self.node.name_history (name)]
    assert_equal (values, val (["first", "second", "third", "fourth"]))

    # Detach the last block and check that both transactions are restored
    # to the mempool.
    self.node.invalidateblock (self.node.getbestblockhash ())
    self.checkName (0, name, val ("second"))
    assert_equal (self.node.name_pending (name), pending)


if __name__ == '__main__':
  NameMultiUpdateTest ().main ()
