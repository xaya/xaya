#!/usr/bin/env python3
# Copyright (c) 2014-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for basic name registration and access (name_show, name_history).

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameRegistrationTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 2)

  def run_test (self):
    # Perform name_new's.  Check for too long names exception.
    newA = self.nodes[0].name_new ("node-0")
    newAconfl = self.nodes[1].name_new ("node-0")
    newB = self.nodes[1].name_new ("node-1")
    self.nodes[0].name_new ("x" * 255)
    assert_raises_rpc_error (-8, 'name is too long',
                             self.nodes[0].name_new, "x" * 256)
    self.generate (0, 5)

    # Check for exception with name_history and without -namehistory.
    assert_raises_rpc_error (-1, 'namehistory is not enabled',
                             self.nodes[0].name_history, "node-0")

    # first_update the names.  Check for too long values.
    addrA = self.nodes[0].getnewaddress ()
    txidA = self.firstupdateName (0, "node-0", newA, "value-0", addrA)
    assert_raises_rpc_error (-8, 'value is too long',
                             self.firstupdateName, 1, "node-1", newB, "x" * 521)
    self.firstupdateName (1, "node-1", newB, "x" * 520)

    # Check for mempool conflict detection with registration of "node-0".
    self.sync_with_mode ('both')
    assert_raises_rpc_error (-25, 'is already being registered',
                             self.firstupdateName,
                             1, "node-0", newAconfl, "foo")
    
    # Check that the name appears when the name_new is ripe.

    self.generate (0, 7)
    assert_raises_rpc_error (-4, 'name not found',
                             self.nodes[1].name_show, "node-0")
    assert_raises_rpc_error (-4, 'name not found',
                             self.nodes[1].name_history, "node-0")
    self.generate (0, 1)

    data = self.checkName (1, "node-0", "value-0")
    assert_equal (data['address'], addrA)
    assert_equal (data['txid'], txidA)
    assert_equal (data['height'], 213)

    self.checkNameHistory (1, "node-0", ["value-0"])
    self.checkNameHistory (1, "node-1", ["x" * 520])

    # Check for error with rand mismatch (wrong name)
    newA = self.nodes[0].name_new ("test-name")
    self.generate (0, 10)
    assert_raises_rpc_error (-25, 'rand value is wrong',
                             self.firstupdateName,
                             0, "test-name-wrong", newA, "value")

    # Check for mismatch with prev tx from another node for name_firstupdate
    # and name_update.
    assert_raises_rpc_error (-4, 'Input tx not found in wallet',
                             self.firstupdateName,
                             1, "test-name", newA, "value")
    self.firstupdateName (0, "test-name", newA, "test-value")

    # Check for disallowed firstupdate when the name is active.
    newSteal = self.nodes[1].name_new ("node-0")
    newSteal2 = self.nodes[1].name_new ("node-0")
    self.generate (0, 19)
    self.checkName (1, "node-0", "value-0")
    assert_raises_rpc_error (-25, 'this name is already active',
                             self.firstupdateName,
                             1, "node-0", newSteal, "stolen")

    # Check for firstupdating an active name, but this time without the check
    # present in the RPC call itself.  This should still be prevented by the
    # mempool logic.  There was a bug that allowed these transactiosn to get
    # into the mempool, so make sure it is no longer there.
    self.firstupdateName (1, "node-0", newSteal2, "stolen",
                          allowActive = True)
    assert_equal (self.nodes[1].getrawmempool (), [])
    self.checkName (1, "node-0", "value-0")

    # Check basic updating.
    assert_raises_rpc_error (-8, 'value is too long',
                             self.nodes[0].name_update, "test-name", "x" * 521)
    self.nodes[0].name_update ("test-name", "x" * 520)
    self.checkName (0, "test-name", "test-value")
    self.generate (0, 1)
    self.checkName (1, "test-name", "x" * 520)
    self.checkNameHistory (1, "test-name", ["test-value", "x" * 520])

    addrB = self.nodes[1].getnewaddress ()
    self.nodes[0].name_update ("test-name", "sent", addrB)
    self.generate (0, 1)
    data = self.checkName (0, "test-name", "sent")
    assert_equal (data['address'], addrB)
    self.nodes[1].name_update ("test-name", "updated")
    self.generate (0, 1)
    data = self.checkName (0, "test-name", "updated")
    self.checkNameHistory (1, "test-name",
                           ["test-value", "x" * 520, "sent", "updated"])

    # Invalid updates.
    assert_raises_rpc_error (-25, 'this name can not be updated',
                             self.nodes[1].name_update, "wrong-name", "foo")
    assert_raises_rpc_error (-4, 'Input tx not found in wallet',
                             self.nodes[0].name_update, "test-name", "stolen?")

    # Reject update when another update is pending.
    self.nodes[1].name_update ("test-name", "value")
    assert_raises_rpc_error (-25, 'is already a pending update for this name',
                             self.nodes[1].name_update,
                             "test-name", "new value")
    self.generate (0, 1)
    data = self.checkName (0, "test-name", "value")
    self.checkNameHistory (1, "test-name", ["test-value", "x" * 520, "sent",
                                            "updated", "value"])
    
    # Test that name updates are even possible with less balance in the wallet
    # than what is locked in a name (0.01 NMC).  There was a bug preventing
    # this from working.
    self.generate (0, 50)  # make node1 not get maturing rewards
    balance = self.nodes[1].getbalance ()
    keep = Decimal ("0.001")
    addr0 = self.nodes[0].getnewaddress ()
    self.nodes[1].sendtoaddress (addr0, balance - keep, "", "", True)
    addr1 = self.nodes[1].getnewaddress ()
    self.nodes[0].name_update ("node-0", "value", addr1)
    self.generate (0, 1)
    assert_equal (self.nodes[1].getbalance (), keep)
    self.nodes[1].name_update ("node-0", "new value")
    self.generate (0, 1)
    self.checkName (1, "node-0", "new value")
    assert self.nodes[1].getbalance () < Decimal ("0.01")

if __name__ == '__main__':
  NameRegistrationTest ().main ()
