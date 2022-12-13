#!/usr/bin/env python3
# Copyright (c) 2014-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test that reorgs (undoing) work for names.  This also checks that
# cleaning the mempool with respect to conflicting name registrations works.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameReorgTest (NameTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.setup_name_test ([["-namehistory"]])

  def run_test (self):
    node = self.nodes[0]
    self.generate (node, 200)

    # Register a name prior to forking the chain.  This is used
    # to test unrolling of updates (as opposed to registrations).
    node.name_register ("x/a", val ("initial value"))
    self.generate (node, 1)

    # Build a long chain that registers "b" (to clash with
    # the same registration on the short chain).
    node.name_register ("x/b", val ("b long"))
    undoBlk = self.generate (node, 20)[0]
    self.checkName (0, "x/a", val ("initial value"))
    self.checkName (0, "x/b", val ("b long"))
    self.checkNameHistory (0, "x/a", val (["initial value"]))
    self.checkNameHistory (0, "x/b", val (["b long"]))
    node.invalidateblock (undoBlk)

    # Build a short chain with an update to "a" and registrations.
    assert_equal (node.getrawmempool (), [])
    self.generate (node, 1)
    txidA = node.name_update ("x/a", val ("changed value"))
    txidC = node.name_register ("x/c", val ("c registered"))
    # txidB will be the conflicted tx, we do it last to that
    # none of the ones we want resurrected depends on it accidentally.
    txidB = node.name_register ("x/b", val ("b short"))
    self.generate (node, 1)
    self.checkName (0, "x/a", val ("changed value"))
    self.checkName (0, "x/b", val ("b short"))
    self.checkName (0, "x/c", val ("c registered"))
    self.checkNameHistory (0, "x/a", val (["initial value", "changed value"]))
    self.checkNameHistory (0, "x/b", val (["b short"]))
    self.checkNameHistory (0, "x/c", val (["c registered"]))

    # Reconsider the long chain to reorg back to it.
    node.reconsiderblock (undoBlk)
    self.checkName (0, "x/a", val ("initial value"))
    self.checkName (0, "x/b", val ("b long"))
    self.checkNameHistory (0, "x/a", val (["initial value"]))
    self.checkNameHistory (0, "x/b", val (["b long"]))
    assert_raises_rpc_error (-4, 'name never existed', node.name_show, "x/c")
    assert_raises_rpc_error (-4, 'name not found', node.name_history, "x/c")

    # Mine another block.  This should at least perform the
    # non-conflicting transactions.
    assert_equal (set (node.getrawmempool ()), set ([txidA, txidC]))
    self.generate (node, 1)
    self.checkName (0, "x/a", val ("changed value"))
    self.checkName (0, "x/b", val ("b long"))
    self.checkName (0, "x/c", val ("c registered"))
    self.checkNameHistory (0, "x/a", val (["initial value", "changed value"]))
    self.checkNameHistory (0, "x/b", val (["b long"]))
    self.checkNameHistory (0, "x/c", val (["c registered"]))

    # Check that the conflicting tx got handled properly.
    assert_equal (node.getrawmempool (), [])
    data = node.gettransaction (txidB)
    assert data['confirmations'] <= 0


if __name__ == '__main__':
  NameReorgTest ().main ()
