#!/usr/bin/env python3
# Copyright (c) 2019-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the behaviour of multiple updates for one name within a single block.

from test_framework.names import NameTestFramework

from test_framework.util import (
  assert_equal,
  assert_raises_rpc_error,
)


class NameMultiUpdateTest (NameTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.setup_name_test ([["-debug", "-namehistory", "-limitnamechains=10"]])

  def run_test (self):
    self.node = self.nodes[0]
    self.generate (self.node, 110)

    # Register a test name.
    name = "d/test"
    new = self.node.name_new (name)
    self.generate (self.node, 12)
    self.firstupdateName (0, name, new, "first")

    # Building an update on top of a pending registration.
    self.node.name_update (name, "second")

    # Finalise the registration.
    self.generate (self.node, 1)
    self.checkName (0, name, "second", None, False)
    assert_equal (self.node.name_pending (), [])

    # Build two update transactions building on each other.
    txn = []
    txn.append (self.node.name_update (name, "third"))
    txn.append (self.node.name_update (name, "fourth"))

    # Check that both are in the mempool.
    assert_equal (set (self.node.getrawmempool ()), set (txn))
    pending = self.node.name_pending (name)
    pendingNameVal = [(p["name"], p["value"]) for p in pending]
    assert_equal (pendingNameVal, [(name, "third"), (name, "fourth")])

    # Mine transactions and verify the effect.
    self.generate (self.node, 1)
    self.checkName (0, name, "fourth", None, False)
    values = [h["value"] for h in self.node.name_history (name)]
    assert_equal (values, ["first", "second", "third", "fourth"])

    # Detach the last block and check that both transactions are restored
    # to the mempool.
    blk = self.node.getbestblockhash ()
    self.node.invalidateblock (blk)
    self.checkName (0, name, "second", None, False)
    assert_equal (self.node.name_pending (name), pending)
    self.node.reconsiderblock (blk)
    assert_equal (self.node.name_pending (), [])

    # Perform a long chain of updates, which should run into the chain limit.
    for n in range (10):
      self.node.name_update (name, "value %d" % n)
    assert_equal (len (self.node.name_pending (name)), 10)
    assert_raises_rpc_error (-25, "too many pending operations",
                             self.node.name_update, name, "another update")


if __name__ == '__main__':
  NameMultiUpdateTest (__file__).main ()
