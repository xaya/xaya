#!/usr/bin/env python3
# Copyright (c) 2018 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test mining with the getwork-like RPCs.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

from test_framework import auxpow

class AuxpowGetworkTest (BitcoinTestFramework):

  def set_test_params (self):
    self.num_nodes = 1

  def add_options (self, parser):
    parser.add_option ("--segwit", dest="segwit", default=False,
                       action="store_true",
                       help="Test behaviour with SegWit active")

  def run_test (self):
    # Enable mock time to be out of IBD.
    self.enable_mocktime ()

    # Activate segwit if requested.
    if self.options.segwit:
      self.nodes[0].generate (500)

    # Test with getwork and creatework/submitwork.
    self.test_getwork ()
    self.test_create_submit_work ()

  def test_common (self, create, submit):
    """
    Common test code that is shared between the tests for getwork and the
    creatework / submitwork method pair.
    """

    # Verify data that can be found in another way.
    work = create ()
    assert_equal (work['algo'], 'neoscrypt')
    assert_equal (work['height'], self.nodes[0].getblockcount () + 1)
    assert_equal (work['previousblockhash'],
                  self.nodes[0].getblockhash (work['height'] - 1))

    # Invalid format for data.
    assert_raises_rpc_error (-8, None, submit, "x")
    assert_raises_rpc_error (-8, None, submit, "00")

    # Compute invalid work.
    target = auxpow.reverseHex (work['target'])
    solved = auxpow.solveData (work['data'], target, False)
    res = submit (solved)
    assert not res

    # Compute and submit valid work.
    solved = auxpow.solveData (work['data'], target, True)
    res = submit (solved)
    assert res

    # Make sure that the block is indeed accepted.
    height = self.nodes[0].getblockcount ()
    assert_equal (height, work['height'])

    # Check that it paid correctly to the first node.
    t = self.nodes[0].listtransactions ("*", 1)
    assert_equal (len (t), 1)
    t = t[0]
    assert_equal (t['category'], "immature")
    assert t['generated']
    assert_greater_than_or_equal (t['amount'], Decimal ("1"))
    assert_equal (t['confirmations'], 1)

  def test_getwork (self):
    """
    Test the getwork method.
    """

    create = self.nodes[0].getwork
    submit = self.nodes[0].getwork
    self.test_common (create, submit)

    # Ensure that the payout address is changed from one block to the next.
    hash1 = auxpow.mineWorkBlockWithMethods (self.nodes[0], create, submit)
    hash2 = auxpow.mineWorkBlockWithMethods (self.nodes[0], create, submit)
    addr1 = auxpow.getCoinbaseAddr (self.nodes[0], hash1)
    addr2 = auxpow.getCoinbaseAddr (self.nodes[0], hash2)
    assert addr1 != addr2
    info = self.nodes[0].getaddressinfo (addr1)
    assert info['ismine']
    info = self.nodes[0].getaddressinfo (addr2)
    assert info['ismine']

  def test_create_submit_work (self):
    """
    Test the creatework / submitwork method pair.
    """

    # Check for errors with wrong parameters.
    assert_raises_rpc_error (-1, None, self.nodes[0].creatework)
    assert_raises_rpc_error (-5, "Invalid coinbase payout address",
                             self.nodes[0].creatework,
                             "this_an_invalid_address")

    # Fix a coinbase address and construct methods for it.
    coinbaseAddr = self.nodes[0].getnewaddress ()
    def create ():
      return self.nodes[0].creatework (coinbaseAddr)
    submit = self.nodes[0].submitwork

    # Run common tests.
    self.test_common (create, submit)

    # Ensure that the payout address is the one which we specify
    hash1 = auxpow.mineWorkBlockWithMethods (self.nodes[0], create, submit)
    hash2 = auxpow.mineWorkBlockWithMethods (self.nodes[0], create, submit)
    addr1 = auxpow.getCoinbaseAddr (self.nodes[0], hash1)
    addr2 = auxpow.getCoinbaseAddr (self.nodes[0], hash2)
    assert_equal (addr1, coinbaseAddr)
    assert_equal (addr2, coinbaseAddr)

if __name__ == '__main__':
  AuxpowGetworkTest ().main ()
