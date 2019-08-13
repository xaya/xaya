#!/usr/bin/env python3
# Copyright (c) 2014-2019 Daniel Kraft
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
    self.setup_name_test ([[], ["-namehistory"]])

  def run_test (self):
    self.generate (0, 50)
    self.generate (1, 50)
    self.generate (0, 100)

    # Perform name_register's.  Check for too long names exception and
    # too long values.
    addrA = self.nodes[0].getnewaddress ()
    txidA = self.nodes[0].name_register ("x/node-0", val ("value-0"),
                                         {"destAddress": addrA})
    self.nodes[1].name_register ("x/node-1", valueOfLength (2048))
    assert_raises_rpc_error (-8, 'tx-value-too-long',
                             self.nodes[0].name_register,
                             "x/dummy name", valueOfLength (2049))
    self.nodes[0].name_register ("x/" + "x" * 254, val ("value"))
    assert_raises_rpc_error (-8, 'tx-name-too-long',
                             self.nodes[0].name_register,
                             "x/" + "x" * 255, val ("dummy value"))

    # Check for exception with name_history and without -namehistory.
    assert_raises_rpc_error (-1, 'namehistory is not enabled',
                             self.nodes[0].name_history, "x/node-0")

    # Check for mempool conflict detection with registration of "node-0".
    self.sync_with_mode ('both')
    assert_raises_rpc_error (-25, 'is already a pending registration',
                             self.nodes[1].name_register,
                             "x/node-0", val ("foo"))
    
    # Check that the name data appears when the tx are mined.
    self.generate (0, 1)
    data = self.checkName (1, "x/node-0", val ("value-0"))
    assert_equal (data['address'], addrA)
    assert_equal (data['txid'], txidA)
    assert_equal (data['height'], 201)

    self.checkNameHistory (1, "x/node-0", [val ("value-0")])
    self.checkNameHistory (1, "x/node-1", [valueOfLength (2048)])

    # Check for disallowed registration when the name is active.
    self.checkName (1, "x/node-0", val ("value-0"))
    assert_raises_rpc_error (-25, 'exists already',
                             self.nodes[1].name_register,
                             "x/node-0", val ("stolen"))

    # Check basic updating.
    self.nodes[0].name_register ("x/test-name", val ("test-value"))
    self.generate (0, 1)
    assert_raises_rpc_error (-8, 'tx-value-too-long',
                             self.nodes[0].name_update,
                             "x/test-name",
                             valueOfLength (2049))
    self.nodes[0].name_update ("x/test-name", valueOfLength (2048))
    self.checkName (0, "x/test-name", val ("test-value"))
    self.generate (0, 1)
    self.checkName (1, "x/test-name", valueOfLength (2048))

    # In Xaya, we also verify that the value must be valid JSON.
    # It is specifically allowed to have JSON objects with duplicated keys.
    # Verify this is true.
    duplicateKeys = '{"x": 1, "x": 2}'
    self.nodes[0].name_update ("x/test-name", duplicateKeys)
    self.generate (0, 1)
    self.checkName (1, "x/test-name", duplicateKeys)
    self.checkNameHistory (1, "x/test-name", [
      val ("test-value"),
      valueOfLength (2048),
      duplicateKeys,
    ])

    addrB = self.nodes[1].getnewaddress ()
    self.nodes[0].name_update ("x/test-name", val ("sent"),
                               {"destAddress": addrB})
    self.generate (0, 1)
    data = self.checkName (0, "x/test-name", val ("sent"))
    assert_equal (data['address'], addrB)
    self.nodes[1].name_update ("x/test-name", val ("updated"))
    self.generate (0, 1)
    data = self.checkName (0, "x/test-name", val ("updated"))
    self.checkNameHistory (1, "x/test-name", [
      val ("test-value"),
      valueOfLength (2048),
      duplicateKeys,
      val ("sent"),
      val ("updated"),
    ])

    # Invalid updates.
    assert_raises_rpc_error (-25, 'this name can not be updated',
                             self.nodes[1].name_update,
                             "x/wrong-name", val ("foo"))
    assert_raises_rpc_error (-4, 'Input tx not found in wallet',
                             self.nodes[0].name_update,
                             "x/test-name", val ("stolen?"))

    # Test that name updates are even possible with less balance in the wallet
    # than what is locked in a name (0.01 NMC).  There was a bug preventing
    # this from working.
    self.generate (0, 50)  # make node1 not get maturing rewards
    balance = self.nodes[1].getbalance ()
    keep = Decimal ("0.001")
    addr0 = self.nodes[0].getnewaddress ()
    self.nodes[1].sendtoaddress (addr0, balance - keep, "", "", True)
    addr1 = self.nodes[1].getnewaddress ()
    self.nodes[0].name_update ("x/node-0", val ("value"),
                               {"destAddress": addr1})
    self.generate (0, 1)
    assert_equal (self.nodes[1].getbalance (), keep)
    self.nodes[1].name_update ("x/node-0", val ("new value"))
    self.generate (0, 1)
    self.checkName (1, "x/node-0", val ("new value"))
    assert self.nodes[1].getbalance () < Decimal ("0.01")

if __name__ == '__main__':
  NameRegistrationTest ().main ()
