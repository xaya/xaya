#!/usr/bin/env python3
# Copyright (c) 2014-2018 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for the name scanning functions (name_scan and name_filter).

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameScanningTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 1)

  def run_test (self):
    self.node = self.nodes[0]

    # Mine a block so that we're no longer in initial download.
    self.node.generate (1)

    # Initially, all should be empty.
    assert_equal (self.node.name_scan (), [])
    assert_equal (self.node.name_scan ("foo", 10), [])
    assert_equal (self.node.name_filter (), [])
    assert_equal (self.node.name_filter ("", 0, 0, 0, "stat"),
                  {"blocks": 201,"count": 0})

    # Register some names with various data, heights and expiration status.
    # Using both "aa" and "b" ensures that we can also check for the expected
    # comparison order between string length and lexicographic ordering.

    newA = self.node.name_new ("a")
    newAA = self.node.name_new ("aa")
    newB = self.node.name_new ("b")
    newC = self.node.name_new ("c")
    self.node.generate (15)

    self.firstupdateName (0, "a", newA, "wrong value")
    self.firstupdateName (0, "aa", newAA, "value aa")
    self.firstupdateName (0, "b", newB, "value b")
    self.node.generate (15)
    self.firstupdateName (0, "c", newC, "value c")
    self.node.name_update ("a", "value a")
    self.node.generate (20)

    # Check the expected name_scan data values.
    scan = self.node.name_scan ()
    assert_equal (len (scan), 4)
    self.checkNameData (scan[0], "a", "value a", 11, False)
    self.checkNameData (scan[1], "b", "value b", -4, True)
    self.checkNameData (scan[2], "c", "value c", 11, False)
    self.checkNameData (scan[3], "aa", "value aa", -4, True)

    # Check for expected names in various name_scan calls.
    self.checkList (self.node.name_scan (), ["a", "b", "c", "aa"])
    self.checkList (self.node.name_scan ("", 0), [])
    self.checkList (self.node.name_scan ("", -1), [])
    self.checkList (self.node.name_scan ("b"), ["b", "c", "aa"])
    self.checkList (self.node.name_scan ("zz"), [])
    self.checkList (self.node.name_scan ("", 2), ["a", "b"])
    self.checkList (self.node.name_scan ("b", 1), ["b"])

    # Check the expected name_filter data values.
    scan = self.node.name_scan ()
    assert_equal (len (scan), 4)
    self.checkNameData (scan[0], "a", "value a", 11, False)
    self.checkNameData (scan[1], "b", "value b", -4, True)
    self.checkNameData (scan[2], "c", "value c", 11, False)
    self.checkNameData (scan[3], "aa", "value aa", -4, True)

    # Check for expected names in various name_filter calls.
    height = self.node.getblockcount ()
    self.checkList (self.node.name_filter (), ["a", "b", "c", "aa"])
    self.checkList (self.node.name_filter ("[ac]"), ["a", "c", "aa"])
    self.checkList (self.node.name_filter ("", 10), [])
    self.checkList (self.node.name_filter ("", 30), ["a", "c"])
    self.checkList (self.node.name_filter ("", 0, 0, 0),
                    ["a", "b", "c", "aa"])
    self.checkList (self.node.name_filter ("", 0, 0, 1), ["a"])
    self.checkList (self.node.name_filter ("", 0, 1, 4), ["b", "c", "aa"])
    self.checkList (self.node.name_filter ("", 0, 4, 4), [])
    assert_equal (self.node.name_filter ("", 30, 0, 0, "stat"),
                  {"blocks": height, "count": 2})

    # Check test for "stat" argument.
    assert_raises_rpc_error (-8, "must be the literal string 'stat'",
                             self.node.name_filter, "", 0, 0, 0, "string")

    # Include a name with invalid UTF-8 to make sure it doesn't break
    # name_filter's regexp check.
    self.restart_node (0, extra_args=["-nameencoding=hex"])
    hexName = "642f00ff"
    new = self.node.name_new (hexName)
    self.node.generate (10)
    self.firstupdateName (0, hexName, new, "{}")
    self.node.generate (5)
    fullHexList = ['61', '62', '63', '6161', hexName]
    self.checkList (self.node.name_scan (), fullHexList)
    self.checkList (self.node.name_filter (), fullHexList)
    self.checkList (self.node.name_filter ("a"), ['61', '6161'])

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
