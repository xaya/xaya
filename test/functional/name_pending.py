#!/usr/bin/env python3
# Copyright (c) 2015-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for name_pending call.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameScanningTest (NameTestFramework):

  def run_test (self):
    # Register a name that can then be update'd in the mempool.
    newData = self.nodes[1].name_new ("a")
    self.generate (0, 10)
    self.firstupdateName (1, "a", newData, "old-value-a")
    self.generate (0, 10)

    # Start a new name registration so we can first_update it.
    newData = self.nodes[2].name_new ("b")
    self.generate (0, 15)

    # Perform the unconfirmed updates.  Include a currency transaction
    # and a name_new to check that those are not shown.
    txa = self.nodes[1].name_update ("a", "value-a")
    txb = self.firstupdateName (2, "b", newData, "value-b")
    addrC = self.nodes[3].getnewaddress ()
    self.nodes[2].sendtoaddress (addrC, 1)
    newData = self.nodes[3].name_new ("c")

    # Check that name_show still returns the old value.
    self.checkName (0, "a", "old-value-a", None, False)

    # Check sizes of mempool against name_pending.
    self.sync_with_mode ('mempool')
    mempool = self.nodes[0].getrawmempool ()
    assert_equal (len (mempool), 4)
    pending = self.nodes[0].name_pending ()
    assert_equal (len (pending), 2)

    # Check result of full name_pending (called above).
    for op in pending:
      assert op['txid'] in mempool
      assert not op['ismine']
      if op['name'] == 'a':
        assert_equal (op['op'], 'name_update')
        assert_equal (op['value'], 'value-a')
        assert_equal (op['txid'], txa)
      elif op['name'] == 'b':
        assert_equal (op['op'], 'name_firstupdate')
        assert_equal (op['value'], 'value-b')
        assert_equal (op['txid'], txb)
      else:
        assert False

    # Check name_pending with name filter that does not match any name.
    pending = self.nodes[0].name_pending ('does not exist')
    assert_equal (pending, [])

    # Check name_pending with name filter and ismine.
    self.checkPendingName (1, 'a', 'name_update', 'value-a', txa, True)
    self.checkPendingName (2, 'a', 'name_update', 'value-a', txa, False)

    # Mine a block and check that all mempool is cleared.
    self.generate (0, 1)
    assert_equal (self.nodes[3].getrawmempool (), [])
    assert_equal (self.nodes[3].name_pending (), [])

    # Send a name and check that ismine is handled correctly.
    tx = self.nodes[1].name_update ('a', 'sent-a', addrC)
    self.sync_with_mode ('mempool')
    self.checkPendingName (1, 'a', 'name_update', 'sent-a', tx, False)
    self.checkPendingName (3, 'a', 'name_update', 'sent-a', tx, True)

  def checkPendingName (self, ind, name, op, value, txid, mine):
    """
    Call name_pending on a given name and check that the result
    matches the expected values.
    """

    res = self.nodes[ind].name_pending (name)
    assert_equal (len (res), 1)

    obj = res[0]
    assert_equal (obj['op'], op)
    assert_equal (obj['name'], name)
    assert_equal (obj['value'], value)
    assert_equal (obj['txid'], txid)
    assert_equal (obj['ismine'], mine)

if __name__ == '__main__':
  NameScanningTest ().main ()
