#!/usr/bin/env python3
# Copyright (c) 2014-2016 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for name_list.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameListTest (NameTestFramework):

  def run_test (self):
    NameTestFramework.run_test (self)

    assert_equal (self.nodes[0].name_list (), [])
    assert_equal (self.nodes[1].name_list (), [])

    newA = self.nodes[0].name_new ("name-a")
    newB = self.nodes[1].name_new ("name-b");
    self.generate (0, 10)
    self.firstupdateName (0, "name-a", newA, "value-a")
    self.firstupdateName (1, "name-b", newB, "value-b")
    self.generate (1, 5)

    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "value-a", False, False)
    arr = self.nodes[1].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-b", "value-b", False, False)

    assert_equal (self.nodes[0].name_list ("name-b"), [])
    assert_equal (self.nodes[1].name_list ("name-a"), [])

    # Transfer a name away and check that name_list updates accordingly.

    addrB = self.nodes[1].getnewaddress ()
    self.nodes[0].name_update ("name-a", "enjoy", addrB)
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "value-a", False, False)

    self.generate (0, 1)
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "enjoy", False, True)
    arr = self.nodes[1].name_list ()
    assert_equal (len (arr), 2)
    self.checkNameStatus (arr[0], "name-a", "enjoy", False, False)
    self.checkNameStatus (arr[1], "name-b", "value-b", False, False)

    # Updating the name in the new wallet shouldn't change the
    # old wallet's name_list entry.
    self.nodes[1].name_update ("name-a", "new value")
    self.generate (0, 1)
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "enjoy", False, True)
    arr = self.nodes[1].name_list ("name-a")
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "new value", False, False)

    # Transfer it back and see that it updates in wallet A.
    addrA = self.nodes[0].getnewaddress ()
    self.nodes[1].name_update ("name-a", "sent", addrA)
    self.generate (0, 1)
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "name-a", "sent", False, False)

    # Let name-b expire.
    self.generate (0, 25)
    arr = self.nodes[1].name_list ()
    assert_equal (len (arr), 2)
    self.checkNameStatus (arr[0], "name-a", "sent", False, True)
    self.checkNameStatus (arr[1], "name-b", "value-b", True, False)

  def checkNameStatus (self, data, name, value, expired, transferred):
    """
    Check a name_list entry for the expected data.
    """

    self.checkNameData (data, name, value, None, expired)
    assert_equal (data['transferred'], transferred)

if __name__ == '__main__':
  NameListTest ().main ()
