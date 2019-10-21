#!/usr/bin/env python3
# Copyright (c) 2014-2019 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test that reorgs (undoing) work for names.  This also checks that
# cleaning the mempool with respect to conflicting name registrations works.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameReorgTest (NameTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.setup_name_test ([["-namehistory"]])

  def run_test (self):
    node = self.nodes[0]
    node.generate (200)

    # Register a name prior to forking the chain.  This is used
    # to test unrolling of updates (as opposed to registrations).
    newA = node.name_new ("a")
    newBshort = node.name_new ("b")
    newBlong = node.name_new ("b")
    newC = node.name_new ("c")
    node.generate (10)
    self.firstupdateName (0, "a", newA, "initial value")
    node.generate (5)

    # Build a long chain that registers "b" (to clash with
    # the same registration on the short chain).
    self.firstupdateName (0, "b", newBlong, "b long")
    undoBlk = node.generate (20)[0]
    self.checkName (0, "a", "initial value", None, False)
    self.checkName (0, "b", "b long", None, False)
    self.checkNameHistory (0, "a", ["initial value"])
    self.checkNameHistory (0, "b", ["b long"])
    node.invalidateblock (undoBlk)

    # Build a short chain with an update to "a" and registrations.
    assert_equal (node.getrawmempool (), [])
    node.generate (1)
    txidA = node.name_update ("a", "changed value")
    txidB = self.firstupdateName (0, "b", newBshort, "b short")
    txidC = self.firstupdateName (0, "c", newC, "c registered")
    node.generate (1)
    self.checkName (0, "a", "changed value", None, False)
    self.checkName (0, "b", "b short", None, False)
    self.checkName (0, "c", "c registered", None, False)
    self.checkNameHistory (0, "a", ["initial value", "changed value"])
    self.checkNameHistory (0, "b", ["b short"])
    self.checkNameHistory (0, "c", ["c registered"])

    # Reconsider the long chain to reorg back to it.
    node.reconsiderblock (undoBlk)
    self.checkName (0, "a", "initial value", None, False)
    self.checkName (0, "b", "b long", None, False)
    self.checkNameHistory (0, "a", ["initial value"])
    self.checkNameHistory (0, "b", ["b long"])
    assert_raises_rpc_error (-4, 'name not found', node.name_show, "c")
    assert_raises_rpc_error (-4, 'name not found', node.name_history, "c")

    # Mine another block.  This should at least perform the
    # non-conflicting transactions.
    assert_equal (set (node.getrawmempool ()), set ([txidA, txidC]))
    node.generate (1)
    self.checkName (0, "a", "changed value", None, False)
    self.checkName (0, "b", "b long", None, False)
    self.checkName (0, "c", "c registered", None, False)
    self.checkNameHistory (0, "a", ["initial value", "changed value"])
    self.checkNameHistory (0, "b", ["b long"])
    self.checkNameHistory (0, "c", ["c registered"])

    # Check that the conflicting tx got handled properly.
    assert_equal (node.getrawmempool (), [])
    data = node.gettransaction (txidB)
    assert data['confirmations'] <= 0


if __name__ == '__main__':
  NameReorgTest ().main ()
