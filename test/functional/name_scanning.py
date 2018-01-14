#!/usr/bin/env python3
# Copyright (c) 2014-2018 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for the name scanning functions (name_scan and name_filter).

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameScanningTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ()

  def run_test (self):
    # Mine a block so that we're no longer in initial download.
    self.generate (3, 1)

    # Initially, all should be empty.
    assert_equal (self.nodes[0].name_scan (), [])
    assert_equal (self.nodes[0].name_scan ("foo", 10), [])
    assert_equal (self.nodes[0].name_filter (), [])
    assert_equal (self.nodes[0].name_filter ("", 0, 0, 0, "stat"),
                  {"blocks": 201,"count": 0})

    # Register some names with various data and heights.
    # Using both "aa" and "b" ensures that we can also check for the expected
    # comparison order between string length and lexicographic ordering.

    self.nodes[0].name_register ("a", "wrong value")
    self.nodes[0].name_register ("aa", "value aa")
    self.nodes[1].name_register ("b", "value b")
    self.generate (3, 15)
    self.nodes[2].name_register ("c", "value c")
    self.nodes[0].name_update ("a", "value a")
    self.generate (3, 20)

    # Check the expected name_scan data values.
    scan = self.nodes[3].name_scan ()
    assert_equal (len (scan), 4)
    self.checkNameData (scan[0], "a", "value a")
    self.checkNameData (scan[1], "b", "value b")
    self.checkNameData (scan[2], "c", "value c")
    self.checkNameData (scan[3], "aa", "value aa")

    # Check for expected names in various name_scan calls.
    self.checkList (self.nodes[3].name_scan (), ["a", "b", "c", "aa"])
    self.checkList (self.nodes[3].name_scan ("", 0), [])
    self.checkList (self.nodes[3].name_scan ("", -1), [])
    self.checkList (self.nodes[3].name_scan ("b"), ["b", "c", "aa"])
    self.checkList (self.nodes[3].name_scan ("zz"), [])
    self.checkList (self.nodes[3].name_scan ("", 2), ["a", "b"])
    self.checkList (self.nodes[3].name_scan ("b", 1), ["b"])

    # Check the expected name_filter data values.
    scan = self.nodes[3].name_scan ()
    assert_equal (len (scan), 4)
    self.checkNameData (scan[0], "a", "value a")
    self.checkNameData (scan[1], "b", "value b")
    self.checkNameData (scan[2], "c", "value c")
    self.checkNameData (scan[3], "aa", "value aa")

    # Check for expected names in various name_filter calls.
    height = self.nodes[3].getblockcount ()
    self.checkList (self.nodes[3].name_filter (), ["a", "b", "c", "aa"])
    self.checkList (self.nodes[3].name_filter ("[ac]"), ["a", "c", "aa"])
    self.checkList (self.nodes[3].name_filter ("", 10), [])
    self.checkList (self.nodes[3].name_filter ("", 30), ["a", "c"])
    self.checkList (self.nodes[3].name_filter ("", 0, 0, 0),
                    ["a", "b", "c", "aa"])
    self.checkList (self.nodes[3].name_filter ("", 0, 0, 1), ["a"])
    self.checkList (self.nodes[3].name_filter ("", 0, 1, 4), ["b", "c", "aa"])
    self.checkList (self.nodes[3].name_filter ("", 0, 4, 4), [])
    assert_equal (self.nodes[3].name_filter ("", 30, 0, 0, "stat"),
                  {"blocks": height, "count": 2})

    # Check test for "stat" argument.
    assert_raises_rpc_error (-8, "must be the literal string 'stat'",
                             self.nodes[3].name_filter, "", 0, 0, 0, "string")

  def checkList (self, data, names):
    """
    Check that the result in 'data' contains the names
    given in the array 'names'.
    """

    def walker (e):
      return e['name']
    dataNames = list (map (walker, data))

    assert_equal (dataNames, names)

if __name__ == '__main__':
  NameScanningTest ().main ()
