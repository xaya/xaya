#!/usr/bin/env python3
# Copyright (c) 2014-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test that reorgs (undoing) work for names.  This also checks that
# cleaning the mempool with respect to conflicting name registrations works.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameRegistrationTest (NameTestFramework):

  def run_test (self):
    # Register a name prior to forking the chain.  This is used
    # to test unrolling of updates (as opposed to registrations).
    newA = self.nodes[3].name_new ("a")
    newBshort = self.nodes[3].name_new ("b")
    newBlong = self.nodes[0].name_new ("b")
    newC = self.nodes[3].name_new ("c")
    self.generate (0, 10)
    self.firstupdateName (3, "a", newA, "initial value")
    self.generate (0, 5)

    # Split the network.
    self.split_network ()

    # Build a long chain that registers "b" (to clash with
    # the same registration on the short chain).
    self.generate (0, 2)
    self.firstupdateName (0, "b", newBlong, "b long")
    self.generate (0, 2)
    self.checkName (0, "a", "initial value", None, False)
    self.checkName (0, "b", "b long", None, False)
    self.checkNameHistory (1, "a", ["initial value"])
    self.checkNameHistory (1, "b", ["b long"])

    # Build a short chain with an update to "a" and registrations.
    self.generate (3, 1)
    txidB = self.firstupdateName (3, "b", newBshort, "b short")
    self.firstupdateName (3, "c", newC, "c registered")
    self.nodes[3].name_update ("a", "changed value")
    self.generate (3, 1)
    self.checkName (3, "a", "changed value", None, False)
    self.checkName (3, "b", "b short", None, False)
    self.checkName (3, "c", "c registered", None, False)
    self.checkNameHistory (2, "a", ["initial value", "changed value"])
    self.checkNameHistory (2, "b", ["b short"])
    self.checkNameHistory (2, "c", ["c registered"])

    # Join the network and let the long chain prevail.
    self.join_network ()
    self.checkName (3, "a", "initial value", None, False)
    self.checkName (3, "b", "b long", None, False)
    self.checkNameHistory (2, "a", ["initial value"])
    self.checkNameHistory (2, "b", ["b long"])
    assert_raises_jsonrpc (-4, 'name not found', self.nodes[3].name_show, "c")
    assert_raises_jsonrpc (-4, 'name not found',
                           self.nodes[2].name_history, "c")

    # Mine another block.  This should at least perform the
    # non-conflicting transactions.  It is done on node 3 so
    # that these tx are actually in the mempool.
    self.generate (3, 1, False)
    self.checkName (3, "a", "changed value", None, False)
    self.checkName (3, "b", "b long", None, False)
    self.checkName (3, "c", "c registered", None, False)
    self.checkNameHistory (2, "a", ["initial value", "changed value"])
    self.checkNameHistory (2, "b", ["b long"])
    self.checkNameHistory (2, "c", ["c registered"])

    # Check that the conflicting tx got pruned from the mempool properly
    # and is marked as conflicted in the wallet.
    assert_equal (self.nodes[0].getrawmempool (), [])
    assert_equal (self.nodes[3].getrawmempool (), [])
    data = self.nodes[3].gettransaction (txidB)
    assert data['confirmations'] < 0

if __name__ == '__main__':
  NameRegistrationTest ().main ()
