#!/usr/bin/env python3
# Copyright (c) 2019 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the behaviour of multiple updates for one name within a single block.

from test_framework.names import NameTestFramework, val, buildMultiUpdateBlock

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
    self.node.name_register (name, val ("first"))
    self.node.generate (1)
    self.checkName (0, name, val ("first"))

    # Update the name (keep the transaction pending) and check that another
    # update is not allowed by the mempool.
    txid = self.node.name_update (name, val ("second"))
    assert_raises_rpc_error (-25, "already a pending update",
                             self.node.name_update, name, val ("wrong"))
    self.node.generate (1)
    self.checkName (0, name, val ("second"))

    # Build two update transactions building on each other and try to
    # submit them both as transactions.
    blk = buildMultiUpdateBlock (self.node, name, val (["third", "wrong"]))
    assert_equal (len (blk.vtx), 3)
    self.node.sendrawtransaction (blk.vtx[1].serialize ().hex ())
    assert_raises_rpc_error (-26, "txn-mempool-name-error",
                             self.node.sendrawtransaction,
                             blk.vtx[2].serialize ().hex ())
    self.node.generate (1)
    self.checkName (0, name, val ("third"))

    # Submitting two updates at once in a block is fine as long as that does
    # not go through the mempool.
    blk = buildMultiUpdateBlock (self.node, name, val (["fourth", "fifth"]))
    blkHex = blk.serialize (with_witness=False).hex ()
    assert_equal (self.node.submitblock (blkHex), None)

    # Verify that the second update took effect, as well as the entire
    # history of the name.
    self.checkName (0, name, val ("fifth"))
    values = [h["value"] for h in self.node.name_history (name)]
    assert_equal (values, val (["first", "second", "third", "fourth", "fifth"]))


if __name__ == '__main__':
  NameMultiUpdateTest ().main ()
