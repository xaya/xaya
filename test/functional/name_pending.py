#!/usr/bin/env python3
# Copyright (c) 2015-2018 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for name_pending call.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameScanningTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ()

  def run_test (self):
    # Register a name that can then be update'd in the mempool.
    self.nodes[1].name_register ("x/a", val ("old-value-a"))
    self.generate (0, 1)

    # Start a new name registration so we can first_update it.
    txb = self.nodes[2].name_register ("x/b", val ("value-b"))

    # Perform the unconfirmed updates.  Include a currency transaction
    # to check that it is not shown.
    txa = self.nodes[1].name_update ("x/a", val ("value-a"))
    addrC = self.nodes[3].getnewaddress ()
    self.nodes[2].sendtoaddress (addrC, 1)

    # Check that name_show still returns the old value.
    self.checkName (0, "x/a", val ("old-value-a"))

    # Check sizes of mempool against name_pending.
    self.sync_with_mode ('mempool')
    mempool = self.nodes[0].getrawmempool ()
    assert_equal (len (mempool), 3)
    pending = self.nodes[0].name_pending ()
    assert_equal (len (pending), 2)

    # Check result of full name_pending (called above).
    for op in pending:
      assert op['txid'] in mempool
      assert not op['ismine']
      if op['name'] == 'x/a':
        assert_equal (op['op'], 'name_update')
        assert_equal (op['value'], val ('value-a'))
        assert_equal (op['txid'], txa)
      elif op['name'] == 'x/b':
        assert_equal (op['op'], 'name_register')
        assert_equal (op['value'], val ('value-b'))
        assert_equal (op['txid'], txb)
      else:
        assert False

    # Check name_pending with name filter that does not match any name.
    pending = self.nodes[0].name_pending ('x/does not exist')
    assert_equal (pending, [])

    # Check name_pending with name filter and ismine.
    self.checkPendingName (1, 'x/a', 'name_update', val ('value-a'), txa, True)
    self.checkPendingName (2, 'x/a', 'name_update', val ('value-a'), txa, False)

    # Mine a block and check that all mempool is cleared.
    self.generate (0, 1)
    assert_equal (self.nodes[3].getrawmempool (), [])
    assert_equal (self.nodes[3].name_pending (), [])

    # Send a name and check that ismine is handled correctly.
    tx = self.nodes[1].name_update ('x/a', val ('sent-a'), addrC)
    self.sync_with_mode ('mempool')
    self.checkPendingName (1, 'x/a', 'name_update', val ('sent-a'), tx, False)
    self.checkPendingName (3, 'x/a', 'name_update', val ('sent-a'), tx, True)

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
    assert isinstance (obj['ismine'], bool)
    assert_equal (obj['ismine'], mine)

if __name__ == '__main__':
  NameScanningTest ().main ()
