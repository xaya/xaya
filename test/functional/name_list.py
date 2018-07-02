#!/usr/bin/env python3
# Copyright (c) 2014-2018 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for name_list.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameListTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 2)

  def run_test (self):
    assert_equal (self.nodes[0].name_list (), [])
    assert_equal (self.nodes[1].name_list (), [])

    self.nodes[0].name_register ("x/name-a", val ("value-a"))
    self.nodes[1].name_register ("x/name-b", val ("value-b"))
    self.generate (1, 1)

    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "x/name-a", val ("value-a"), True)
    arr = self.nodes[1].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "x/name-b", val ("value-b"), True)

    assert_equal (self.nodes[0].name_list ("x/name-b"), [])
    assert_equal (self.nodes[1].name_list ("x/name-a"), [])

    # Transfer a name away and check that name_list updates accordingly.

    addrB = self.nodes[1].getnewaddress ()
    self.nodes[0].name_update ("x/name-a", val ("enjoy"),
                               {"destAddress": addrB})
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "x/name-a", val ("value-a"), True)

    self.generate (0, 1)
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "x/name-a", val ("enjoy"), False)
    arr = self.nodes[1].name_list ()
    assert_equal (len (arr), 2)
    self.checkNameStatus (arr[0], "x/name-a", val ("enjoy"), True)
    self.checkNameStatus (arr[1], "x/name-b", val ("value-b"), True)

    # Updating the name in the new wallet shouldn't change the
    # old wallet's name_list entry.
    self.nodes[1].name_update ("x/name-a", val ("new value"))
    self.generate (0, 1)
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "x/name-a", val ("enjoy"), False)
    arr = self.nodes[1].name_list ("x/name-a")
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "x/name-a", val ("new value"), True)

    # Transfer it back and see that it updates in wallet A.
    addrA = self.nodes[0].getnewaddress ()
    self.nodes[1].name_update ("x/name-a", val ("sent"), {"destAddress": addrA})
    self.generate (0, 1)
    arr = self.nodes[0].name_list ()
    assert_equal (len (arr), 1)
    self.checkNameStatus (arr[0], "x/name-a", val ("sent"), True)

  def checkNameStatus (self, data, name, value, mine):
    """
    Check a name_list entry for the expected data.
    """

    self.checkNameData (data, name, value)
    assert_equal (data['ismine'], mine)

if __name__ == '__main__':
  NameListTest ().main ()
