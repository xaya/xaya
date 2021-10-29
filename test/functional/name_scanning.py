#!/usr/bin/env python3
# Copyright (c) 2014-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for the name_scan RPC method.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameScanningTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([["-nameencoding=ascii", "-valueencoding=ascii"]] * 1)

  def run_test (self):
    self.node = self.nodes[0]

    # Mine a block so that we're no longer in initial download.
    self.generate (self.node, 1)

    # Initially, all should be empty.
    assert_equal (self.node.name_scan (), [])
    assert_equal (self.node.name_scan ("foo", 10), [])

    # Register some names with various data and heights.
    # Using both "aa" and "b" ensures that we can also check for the expected
    # comparison order between string length and lexicographic ordering.
    self.node.name_register ("d/a", val ("wrong value"))
    self.node.name_register ("d/aa", val ("value aa"))
    self.node.name_register ("d/b", val ("value b"))
    self.generate (self.node, 15)
    self.node.name_register ("d/c", val ("value c"))
    self.node.name_update ("d/a", val ("value a"))
    self.generate (self.node, 20)

    # Check the expected name_scan data values.
    scan = self.node.name_scan ()
    assert_equal (len (scan), 4)
    self.checkNameData (scan[0], "d/a", val ("value a"))
    self.checkNameData (scan[1], "d/b", val ("value b"))
    self.checkNameData (scan[2], "d/c", val ("value c"))
    self.checkNameData (scan[3], "d/aa", val ("value aa"))

    # Check for expected names in various basic name_scan calls.
    self.checkList (self.node.name_scan (), ["d/a", "d/b", "d/c", "d/aa"])
    self.checkList (self.node.name_scan ("", 0), [])
    self.checkList (self.node.name_scan ("", -1), [])
    self.checkList (self.node.name_scan ("d/b"), ["d/b", "d/c", "d/aa"])
    self.checkList (self.node.name_scan ("d/zz"), [])
    self.checkList (self.node.name_scan ("", 2), ["d/a", "d/b"])
    self.checkList (self.node.name_scan ("d/b", 1), ["d/b"])

    # Verify encoding for start argument.
    self.checkList (self.node.name_scan ("642f63", 10, {"nameEncoding": "hex"}),
                    ["642f63", "642f6161"])

    # Verify filtering based on number of confirmations.
    self.checkList (self.node.name_scan ("", 100, {"minConf": 35}),
                    ["d/b", "d/aa"])
    self.checkList (self.node.name_scan ("", 100, {"minConf": 36}), [])
    self.checkList (self.node.name_scan ("", 100, {"maxConf": 19}), [])
    self.checkList (self.node.name_scan ("", 100, {"maxConf": 20}),
                    ["d/a", "d/c"])

    # Verify interaction with filtering and count.
    self.checkList (self.node.name_scan ("", 1, {"maxConf": 20}), ["d/a"])
    self.checkList (self.node.name_scan ("", 2, {"maxConf": 20}),
                    ["d/a", "d/c"])

    # Error checks for confirmation options.
    assert_raises_rpc_error (-8, "minConf must be >= 1",
                             self.node.name_scan, "", 100, {"minConf": 0})
    assert_raises_rpc_error (-8, "minConf must be >= 1",
                             self.node.name_scan, "", 100, {"minConf": -42})
    self.node.name_scan ("", 100, {"minConf": 1})
    assert_raises_rpc_error (-8, "maxConf must not be negative",
                             self.node.name_scan, "", 100, {"maxConf": -1})
    self.node.name_scan ("", 100, {"maxConf": 0})

    # Verify filtering based on prefix.
    self.checkList (self.node.name_scan ("", 100, {"prefix": ""}),
                    ["d/a", "d/b", "d/c", "d/aa"])
    self.checkList (self.node.name_scan ("", 100, {"prefix": "d/a"}),
                    ["d/a", "d/aa"])

    # Check prefix and nameEncoding.
    options = {
        "prefix": "642f61",
        "nameEncoding": "hex",
    }
    self.checkList (self.node.name_scan ("", 100, options),
                    ["642f61", "642f6161"])
    assert_raises_rpc_error (-1000, "Name/value is invalid for encoding ascii",
                             self.node.name_scan, "", 100, {"prefix": "äöü"})

    # Verify filtering based on regexp.
    self.checkList (self.node.name_scan ("", 100, {"regexp": "[ac]"}),
                    ["d/a", "d/c", "d/aa"])

    # Multiple filters are combined using "and".
    options = {
        "prefix": "d/a",
        "maxConf": 20,
    }
    self.checkList (self.node.name_scan ("", 100, options), ["d/a"])

    # Upstream Namecoin tests here that a name with invalid UTF-8 doesn't
    # break name_filter's regexp check.  In Xaya, this name is invalid,
    # so we can't do this.

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
