#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test that expiring / unexpiring names works as desired, in particular
# with respect to clearing the UTXO set and mempool.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameExpirationTest (NameTestFramework):

  def checkUTXO (self, ind, name, shouldBeThere):
    """
    Query for a name's coin in the UTXO set and check that it is either
    there or not.
    """

    data = self.nodes[ind].name_show (name)
    txo = self.nodes[ind].gettxout (data['txid'], data['vout'])

    if shouldBeThere:
      assert txo is not None
      assert_equal (txo['scriptPubKey']['nameOp']['name'], name)
    else:
      assert txo is None

  def setup_nodes (self):
    return self.setupNodesWithArgs ([["-debug=names"]] * 4)

  def run_test (self):
    NameTestFramework.run_test (self)

    # Start the registration of two names which will be used.  name-long
    # will expire and be reregistered on the short chain, which will be
    # undone with the reorg.  name-short will be updated before expiration
    # on the short chain, but this will be rerolled and the name expire
    # instead on the long chain.  Check that the mempool and the UTXO set
    # behave as they should.
    newLong = self.nodes[0].name_new ("name-long")
    newLong2 = self.nodes[3].name_new ("name-long")
    newShort = self.nodes[3].name_new ("name-short")
    self.generate (1, 12)

    # Register the names.  name-long should expire one block before
    # name-short, so that the situation described above works out.
    updLong = self.firstupdateName (0, "name-long", newLong, "value")
    self.generate (1, 2)
    updShort = self.firstupdateName (3, "name-short", newShort, "value")
    self.generate (1, 27)
    self.checkName (1, "name-long", "value", 2, False)
    self.checkName (1, "name-short", "value", 4, False)

    # Check that the UTXO entries are there.
    self.checkUTXO (1, "name-long", True)
    self.checkUTXO (1, "name-short", True)

    # Split the network.
    self.split_network ()

    # Let name-long expire on the short chain.
    self.generate (2, 2)
    self.checkName (2, "name-long", "value", 0, True)
    self.checkName (2, "name-short", "value", 2, False)
    self.checkUTXO (2, "name-long", False)
    self.checkUTXO (2, "name-short", True)

    # Snatch up name-long and update name-short just-in-time.  Note that
    # "just-in-time" is "expires_in == 2", since when creating the block,
    # it will be "expires_in == 1" already!
    updLong2 = self.firstupdateName (3, "name-long", newLong2, "value 2")
    renewShort = self.nodes[3].name_update ("name-short", "renewed")
    self.generate (2, 1)
    self.checkName (2, "name-long", "value 2", 30, False)
    self.checkName (2, "name-short", "renewed", 30, False)
    self.checkNameHistory (2, "name-long", ["value", "value 2"])
    self.checkNameHistory (2, "name-short", ["value", "renewed"])

    # Create a longer chain on the other part of the network.  Let name-short
    # expire there but renew name-long instead.
    self.nodes[0].name_update ("name-long", "renewed")
    self.generate (1, 5)
    self.checkName (1, "name-long", "renewed", 26, False)
    self.checkName (1, "name-short", "value", -1, True)
    self.checkNameHistory (1, "name-long", ["value", "renewed"])
    self.checkNameHistory (1, "name-short", ["value"])
    self.checkUTXO (1, "name-long", True)
    self.checkUTXO (1, "name-short", False)

    # Join the network and let the long chain prevail.  This should
    # completely revoke all changes on the short chain, including
    # the mempool (since all tx there are conflicts with name expirations).
    assert self.nodes[1].getblockcount () > self.nodes[2].getblockcount ()
    self.join_network ()

    # Test the expected situation of the long chain.
    self.checkName (2, "name-long", "renewed", 26, False)
    self.checkName (2, "name-short", "value", -1, True)
    self.checkNameHistory (2, "name-long", ["value", "renewed"])
    self.checkNameHistory (2, "name-short", ["value"])
    self.checkUTXO (2, "name-long", True)
    self.checkUTXO (2, "name-short", False)

    # Check that the conflicting tx's are marked in the wallet
    # as conflicts and that they got removed from the mempool.
    assert_equal (self.nodes[0].getrawmempool (), [])
    assert_equal (self.nodes[3].getrawmempool (), [])
    data = self.nodes[3].gettransaction (updLong2)
    assert data['confirmations'] < 0
    data = self.nodes[3].gettransaction (renewShort)
    assert data['confirmations'] < 0

if __name__ == '__main__':
  NameExpirationTest ().main ()
