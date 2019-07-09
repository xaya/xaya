#!/usr/bin/env python3
# Copyright (c) 2019 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the behaviour of multiple updates for one name within a single block.

from test_framework.names import NameTestFramework, buildMultiUpdate

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
    new = self.node.name_new (name)
    self.node.generate (12)
    self.firstupdateName (0, name, new, "first")

    # Building an update on top of the pending registration is not allowed
    # by RPC, but is fine with the mempool itself.
    assert_raises_rpc_error (-25, "already a pending operation",
                             self.node.name_update, name, "wrong")
    txn = buildMultiUpdate (self.node, name, ["second"])
    self.node.sendrawtransaction (txn[0].serialize ().hex ())

    # Finalise the registration.
    self.node.generate (1)
    self.checkName (0, name, "second", None, False)
    assert_equal (self.node.name_pending (), [])

    # Update the name (keep the transaction pending) and check that another
    # update by RPC is not allowed.
    txid = self.node.name_update (name, "third")
    assert_raises_rpc_error (-25, "already a pending operation",
                             self.node.name_update, name, "wrong")
    self.node.generate (1)
    self.checkName (0, name, "third", None, False)
    assert_equal (self.node.name_pending (), [])

    # Build two update transactions building on each other and submit
    # them directly to the mempool (bypassing the name_update check).
    txn = buildMultiUpdate (self.node, name, ["fourth", "fifth"])
    for tx in txn:
      self.node.sendrawtransaction (tx.serialize ().hex ())

    # Check that both are in the mempool.
    assert_equal (set (self.node.getrawmempool ()), set ([
      tx.hash for tx in txn
    ]))
    pending = self.node.name_pending (name)
    pendingNameVal = [(p["name"], p["value"]) for p in pending]
    assert_equal (pendingNameVal, [(name, "fourth"), (name, "fifth")])

    # Mine transactions and verify the effect.
    self.node.generate (1)
    self.checkName (0, name, "fifth", None, False)
    values = [h["value"] for h in self.node.name_history (name)]
    assert_equal (values, ["first", "second", "third", "fourth", "fifth"])

    # Detach the last block and check that both transactions are restored
    # to the mempool.
    self.node.invalidateblock (self.node.getbestblockhash ())
    self.checkName (0, name, "third", None, False)
    assert_equal (self.node.name_pending (name), pending)


if __name__ == '__main__':
  NameMultiUpdateTest ().main ()
