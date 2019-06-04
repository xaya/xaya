#!/usr/bin/env python3
# Copyright (c) 2014-2019 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test that reorgs (undoing) work for names.  This also checks that
# cleaning the mempool with respect to conflicting name registrations works.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameRegistrationTest (NameTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.setup_name_test ([["-namehistory"]] * 4)

  def run_test (self):
    self.generate (0, 25)
    self.generate (1, 25)
    self.generate (2, 25)
    self.generate (3, 25)
    self.generate (0, 100)

    # Register a name prior to forking the chain.  This is used
    # to test unrolling of updates (as opposed to registrations).
    self.nodes[3].name_register ("x/a", val ("initial value"))
    self.generate (0, 1)

    # Split the network.
    self.split_network ()

    # Build a long chain that registers "b" (to clash with
    # the same registration on the short chain).
    self.generate (0, 2)
    self.nodes[0].name_register ("x/b", val ("b long"))
    self.generate (0, 2)
    self.checkName (0, "x/a", val ("initial value"))
    self.checkName (0, "x/b", val ("b long"))
    self.checkNameHistory (1, "x/a", [val ("initial value")])
    self.checkNameHistory (1, "x/b", [val ("b long")])

    # Build a short chain with an update to "a" and registrations.
    self.generate (3, 1)
    txidB = self.nodes[3].name_register ("x/b", val ("b short"))
    self.nodes[3].name_register ("x/c", val ("c registered"))
    self.nodes[3].name_update ("x/a", val ("changed value"))
    self.generate (3, 1)
    self.checkName (3, "x/a", val ("changed value"))
    self.checkName (3, "x/b", val ("b short"))
    self.checkName (3, "x/c", val ("c registered"))
    self.checkNameHistory (2, "x/a",
                           [val ("initial value"), val ("changed value")])
    self.checkNameHistory (2, "x/b", [val ("b short")])
    self.checkNameHistory (2, "x/c", [val ("c registered")])

    # Join the network and let the long chain prevail.
    self.join_network ()
    self.checkName (3, "x/a", val ("initial value"))
    self.checkName (3, "x/b", val ("b long"))
    self.checkNameHistory (2, "x/a", [val ("initial value")])
    self.checkNameHistory (2, "x/b", [val ("b long")])
    assert_raises_rpc_error (-4, 'name not found',
                             self.nodes[3].name_show, "x/c")
    assert_raises_rpc_error (-4, 'name not found',
                             self.nodes[2].name_history, "x/c")

    # Mine another block.  This should at least perform the
    # non-conflicting transactions.  It is done on node 3 so
    # that these tx are actually in the mempool.
    self.generate (3, 1, False)
    self.checkName (3, "x/a", val ("changed value"))
    self.checkName (3, "x/b", val ("b long"))
    self.checkName (3, "x/c", val ("c registered"))
    self.checkNameHistory (2, "x/a",
                           [val ("initial value"), val ("changed value")])
    self.checkNameHistory (2, "x/b", [val ("b long")])
    self.checkNameHistory (2, "x/c", [val ("c registered")])

    # Check that the conflicting tx got pruned from the mempool properly.
    assert_equal (self.nodes[0].getrawmempool (), [])
    assert_equal (self.nodes[3].getrawmempool (), [])
    data = self.nodes[3].gettransaction (txidB)
    assert data['confirmations'] <= 0

if __name__ == '__main__':
  NameRegistrationTest ().main ()
