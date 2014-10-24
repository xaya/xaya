#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for name_list.

# Add python-bitcoinrpc to module search path:
import os
import sys
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-bitcoinrpc"))

from bitcoinrpc.authproxy import JSONRPCException
from names import NameTestFramework
from util import assert_equal

class NameListTest (NameTestFramework):

  def run_test (self, nodes):
    # TODO: call super class

    assert_equal (nodes[0].name_list (), [])
    assert_equal (nodes[1].name_list (), [])

    newA = nodes[0].name_new ("name-a")
    newB = nodes[1].name_new ("name-b");
    self.generate (nodes, 0, 10)
    self.firstupdateName (nodes[0], "name-a", newA, "value-a")
    self.firstupdateName (nodes[1], "name-b", newB, "value-b")
    self.generate (nodes, 1, 5)

    arr = nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "value-a", False, False)
    arr = nodes[1].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-b", "value-b", False, False)

    assert_equal (nodes[0].name_list ("name-b"), [])
    assert_equal (nodes[1].name_list ("name-a"), [])

    # Transfer a name away and check that name_list updates accordingly.

    addrB = nodes[1].getnewaddress ()
    nodes[0].name_update ("name-a", "enjoy", addrB)
    arr = nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "value-a", False, False)

    self.generate (nodes, 0, 1)
    arr = nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "enjoy", False, True)
    arr = nodes[1].name_list ()
    assert_equal (len (arr), 2)
    self.checkNameStatus (arr[0], "name-a", "enjoy", False, False)
    self.checkNameStatus (arr[1], "name-b", "value-b", False, False)

    # Updating the name in the new wallet shouldn't change the
    # old wallet's name_list entry.
    nodes[1].name_update ("name-a", "new value")
    self.generate (nodes, 0, 1)
    arr = nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "enjoy", False, True)
    arr = nodes[1].name_list ("name-a")
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "new value", False, False)

    # Transfer it back and see that it updates in wallet A.
    addrA = nodes[0].getnewaddress ()
    nodes[1].name_update ("name-a", "sent", addrA)
    self.generate (nodes, 0, 1)
    arr = nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "sent", False, False)

    # Let name-b expire.
    self.generate (nodes, 0, 25)
    arr = nodes[1].name_list ()
    assert_equal (len (arr), 2)
    self.checkNameStatus (arr[0], "name-a", "sent", False, True)
    self.checkNameStatus (arr[1], "name-b", "value-b", True, False)

  def checkNameStatus (self, data, name, value, expired, transferred):
    """
    Check a name_list entry for the expected data.
    """

    assert_equal (data['name'], name)
    assert_equal (data['value'], value)
    assert_equal (data['expired'], expired)
    assert_equal (data['transferred'], transferred)

if __name__ == '__main__':
  NameListTest ().main ()
