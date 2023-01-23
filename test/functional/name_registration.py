#!/usr/bin/env python3
# Copyright (c) 2014-2023 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for basic name registration and access (name_show, name_history).

from test_framework.names import NameTestFramework, val
from test_framework.util import *


def valueOfLength (size):
  """Returns a valid JSON value for name update of the given length in bytes."""
  prefix = '{"text": "'
  suffix = '"}'
  overhead = len (prefix) + len (suffix)
  assert size > overhead
  result = prefix + 'x' * (size - overhead) + suffix
  assert len (result) == size
  return result


class NameRegistrationTest (NameTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.setup_name_test ([["-namehistory"], []])

  def generateToOther (self, n):
    """
    Generates n blocks on the first node, but to an address of the second
    node (so that the first node does not get any extra coins that could mess
    up our balance tests).
    """

    addr = self.nodes[1].getnewaddress ()
    self.generatetoaddress (self.nodes[0], n, addr)

  def run_test (self):
    node = self.nodes[0]
    self.generate (node, 50)
    self.generateToOther (150)

    # Perform name_register's.  Check for too long names exception and
    # too long values.
    addrA = node.getnewaddress ()
    txidA = node.name_register ("x/name-0", val ("value-0"),
                                {"destAddress": addrA})
    node.name_register ("x/name-1", valueOfLength (2048))
    assert_raises_rpc_error (-8, 'tx-value-too-long',
                             node.name_register,
                             "x/dummy name", valueOfLength (2049))
    node.name_register ("x/" + "x" * 254, val ("value"))
    assert_raises_rpc_error (-8, 'tx-name-too-long',
                             node.name_register,
                             "x/" + "x" * 255, val ("dummy value"))

    # Check for exception with name_history and without -namehistory.
    self.sync_blocks ()
    assert_raises_rpc_error (-1, 'namehistory is not enabled',
                             self.nodes[1].name_history, "x/name-0")

    # Check for mempool conflict detection with registration of "name-0".
    assert_raises_rpc_error (-25, 'is already a pending registration',
                             node.name_register,
                             "x/name-0", val ("foo"))
    
    # Check that the name data appears when the tx are mined.
    assert_raises_rpc_error (-4, 'name never existed',
                             node.name_show, "x/name-0")
    assert_raises_rpc_error (-4, 'name not found',
                             node.name_history, "x/name-0")
    self.generateToOther (1)
    data = self.checkName (1, "x/name-0", val ("value-0"))
    assert_equal (data['address'], addrA)
    assert_equal (data['txid'], txidA)
    assert_equal (data['height'], 201)

    self.checkNameHistory (0, "x/name-0", [val ("value-0")])
    self.checkNameHistory (0, "x/name-1", [valueOfLength (2048)])

    # Check for disallowed registration when the name is active.
    self.checkName (0, "x/name-0", val ("value-0"))
    assert_raises_rpc_error (-25, 'exists already',
                             node.name_register, "x/name-0", val ("stolen"))

    # Check basic updating.
    node.name_register ("x/test-name", val ("test-value"))
    self.generateToOther (1)
    assert_raises_rpc_error (-8, 'tx-value-too-long',
                             node.name_update,
                             "x/test-name", valueOfLength (2049))
    node.name_update ("x/test-name", valueOfLength (2048))
    self.checkName (0, "x/test-name", val ("test-value"))
    self.generateToOther (1)
    self.checkName (0, "x/test-name", valueOfLength (2048))
    self.checkNameHistory (0, "x/test-name",
                           [val ("test-value"), valueOfLength (2048)])

    # In Xaya, we also verify that the value must be valid JSON.
    # It is specifically allowed to have JSON objects with duplicated keys.
    # Verify this is true.
    duplicateKeys = '{"x": 1, "x": 2}'
    node.name_update ("x/test-name", duplicateKeys)
    self.generateToOther (1)
    self.checkName (0, "x/test-name", duplicateKeys)
    self.checkNameHistory (0, "x/test-name", [
      val ("test-value"),
      valueOfLength (2048),
      duplicateKeys,
    ])

    addrOther = self.nodes[1].getnewaddress ()
    node.name_update ("x/test-name", val ("sent"), {"destAddress": addrOther})
    self.generateToOther (1)
    self.sync_blocks ()
    data = self.checkName (0, "x/test-name", val ("sent"))
    assert_equal (data['address'], addrOther)
    self.nodes[1].name_update ("x/test-name", val ("updated"))
    self.generate (self.nodes[1], 1)
    self.sync_blocks ()
    data = self.checkName (1, "x/test-name", val ("updated"))
    self.checkNameHistory (0, "x/test-name", [
      val ("test-value"),
      valueOfLength (2048),
      duplicateKeys,
      val ("sent"),
      val ("updated"),
    ])

    # Invalid updates.
    assert_raises_rpc_error (-25, 'this name can not be updated',
                             node.name_update, "x/wrong-name", val ("foo"))
    assert_raises_rpc_error (-6, 'Not found pre-selected input',
                             node.name_update, "x/test-name", val ("stolen?"))

    # Test that name updates are even possible with less balance in the wallet
    # than what is locked in a name (0.01 NMC).  There was a bug preventing
    # this from working.
    balance = node.getbalance ()
    keep = Decimal ("0.001")
    addrOther = self.nodes[1].getnewaddress ()
    node.sendtoaddress (addrOther, balance - keep, "", "", True)
    self.generateToOther (1)
    assert_equal (node.getbalance (), keep)
    node.name_update ("x/name-1", val ("new value"))
    self.generateToOther (1)
    assert node.getbalance () < Decimal ("0.01")
    self.checkName (0, "x/name-1", val ("new value"))


if __name__ == '__main__':
  NameRegistrationTest ().main ()
