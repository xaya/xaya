#!/usr/bin/env python3
# Copyright (c) 2018-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test for handling of immature inputs for name operations.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameImmatureInputsTest (NameTestFramework):

  def set_test_params (self):
    # We need two nodes so that getblocktemplate doesn't complain about
    # 'not being connected'.  But only node 0 is actually used throughout
    # the test.
    self.setup_name_test ([["-debug=names"]] * 2)

  def dependsOn (self, ind, child, parent):
    """
    Checks whether the child transaction (given by txid) depends on an output
    from the parent txid.
    """

    child = self.nodes[ind].getrawtransaction (child, 1)
    for vin in child['vin']:
      if vin['txid'] == parent:
        return True

    return False

  def run_test (self):
    addr = self.nodes[0].getnewaddress ()

    # Generate some blocks so that node 0 does not get fresh block rewards
    # anymore (similar to how it is in the upstream Namecoin test).
    self.generate (self.nodes[0], 50)

    # It should be possible to use unconfirmed *currency* outputs in a name
    # firstupdate, though (so that multiple name registrations are possible
    # even if one has only a single currency output in the wallet).

    balance = self.nodes[0].getbalance ()
    self.nodes[0].sendtoaddress (addr, balance, None, None, True)
    assert_equal (1, len (self.nodes[0].listunspent (0)))
    firstC = self.nodes[0].name_register ("x/c", val ("value"))
    firstD = self.nodes[0].name_register ("x/d", val ("value"))
    assert self.dependsOn (0, firstD, firstC)
    self.generate (self.nodes[0], 1)
    self.checkName (0, "x/c", val ("value"))
    self.checkName (0, "x/d", val ("value"))

    assert_equal (1, len (self.nodes[0].listunspent ()))
    updC = self.nodes[0].name_update ("x/c", val ("new value"))
    updD = self.nodes[0].name_update ("x/d", val ("new value"))
    assert self.dependsOn (0, updD, updC)
    self.generate (self.nodes[0], 1)
    self.checkName (0, "x/c", val ("new value"))
    self.checkName (0, "x/d", val ("new value"))

if __name__ == '__main__':
  NameImmatureInputsTest ().main ()
