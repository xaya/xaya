#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for the name scanning functions (name_scan and name_filter).

# Add python-bitcoinrpc to module search path:
import os
import sys
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-bitcoinrpc"))

from bitcoinrpc.authproxy import JSONRPCException
from names import NameTestFramework
from util import assert_equal

class NameScanningTest (NameTestFramework):

  def run_test (self):
    NameTestFramework.run_test (self)

    # Initially, all should be empty.
    assert_equal (self.nodes[0].name_scan (), [])
    assert_equal (self.nodes[0].name_scan ("foo", 10), [])
    assert_equal (self.nodes[0].name_filter (), [])
    assert_equal (self.nodes[0].name_filter ("", 0, 0, 0, "stat"),
                  {"blocks": 200,"count": 0})

    # Register some names with various data, heights and expiration status.

    newA = self.nodes[0].name_new ("a")
    newB = self.nodes[1].name_new ("b")
    newC = self.nodes[2].name_new ("c")
    self.generate (3, 15)

    self.firstupdateName (0, "a", newA, "wrong value")
    self.firstupdateName (1, "b", newB, "value b")
    self.generate (3, 15)
    self.firstupdateName (2, "c", newC, "value c")
    self.nodes[0].name_update ("a", "value a")
    self.generate (3, 20)

    # Check the expected name_scan data values.
    scan = self.nodes[3].name_scan ()
    assert_equal (len (scan), 3)
    self.checkNameData (scan[0], "a", "value a", 11, False)
    self.checkNameData (scan[1], "b", "value b", -4, True)
    self.checkNameData (scan[2], "c", "value c", 11, False)

    # Check for expected names in various name_scan calls.
    self.checkList (self.nodes[3].name_scan (), ["a", "b", "c"])
    self.checkList (self.nodes[3].name_scan ("", 0), [])
    self.checkList (self.nodes[3].name_scan ("", -1), [])
    self.checkList (self.nodes[3].name_scan ("b"), ["b", "c"])
    self.checkList (self.nodes[3].name_scan ("z"), [])
    self.checkList (self.nodes[3].name_scan ("", 2), ["a", "b"])
    self.checkList (self.nodes[3].name_scan ("b", 1), ["b"])

    # Check the expected name_filter data values.
    scan = self.nodes[3].name_scan ()
    assert_equal (len (scan), 3)
    self.checkNameData (scan[0], "a", "value a", 11, False)
    self.checkNameData (scan[1], "b", "value b", -4, True)
    self.checkNameData (scan[2], "c", "value c", 11, False)

    # Check for expected names in various name_filter calls.
    height = self.nodes[3].getblockcount ()
    self.checkList (self.nodes[3].name_filter (), ["a", "b", "c"])
    self.checkList (self.nodes[3].name_filter ("[ac]"), ["a", "c"])
    self.checkList (self.nodes[3].name_filter ("", 10), [])
    self.checkList (self.nodes[3].name_filter ("", 30), ["a", "c"])
    self.checkList (self.nodes[3].name_filter ("", 0, 0, 0), ["a", "b", "c"])
    self.checkList (self.nodes[3].name_filter ("", 0, 0, 1), ["a"])
    self.checkList (self.nodes[3].name_filter ("", 0, 1, 3), ["b", "c"])
    self.checkList (self.nodes[3].name_filter ("", 0, 3, 3), [])
    assert_equal (self.nodes[3].name_filter ("", 30, 0, 0, "stat"),
                  {"blocks": height, "count": 2})

    # Check test for "stat" argument.
    try:
      self.nodes[3].name_filter ("", 0, 0, 0, "string")
      raise AssertionError ("wrong check for 'stat' argument")
    except JSONRPCException as exc:
      assert_equal (exc.error['code'], -8)

  def checkList (self, data, names):
    """
    Check that the result in 'data' contains the names
    given in the array 'names'.
    """

    def walker (e):
      return e['name']
    dataNames = map (walker, data)

    assert_equal (dataNames, names)

if __name__ == '__main__':
  NameScanningTest ().main ()
