#!/usr/bin/env python3
# Copyright (c) 2018-2025 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test mining with the getwork-like RPCs.

import codecs

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

from test_framework.auxpow import getworkByteswap, reverseHex
from test_framework.auxpow_testing import (
  getCoinbaseAddr,
  mineWorkBlockWithMethods,
  solveData,
)

class AuxpowGetworkTest (BitcoinTestFramework):

  def set_test_params (self):
    self.num_nodes = 1

  def skip_test_if_missing_module (self):
    self.skip_if_no_wallet ()

  def add_options (self, parser):
    parser.add_argument ("--segwit", dest="segwit", default=False,
                         action="store_true",
                         help="Test behaviour with SegWit active")

  def run_test (self):
    # Activate segwit if requested.
    if self.options.segwit:
      self.generate (self.nodes[0], 500)

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
    assert_raises_rpc_error (-8, None, submit, "", "x")
    assert_raises_rpc_error (-8, None, submit, "", "00")

    # Compute invalid work.
    target = reverseHex (work['target'])
    solved = solveData (work['data'], target, False)
    res = submit (work['hash'], solved)
    assert not res

    # Compute and submit valid work.
    solved = solveData (work['data'], target, True)
    res = submit (work['hash'], solved)
    assert res

    # Make sure that the block is indeed accepted.
    height = self.nodes[0].getblockcount ()
    assert_equal (height, work['height'])

    # Call getblock and verify the powdata field.
    data = self.nodes[0].getblock (work['hash'])
    assert 'powdata' in data
    data = data['powdata']
    assert_equal (data['algo'], 'neoscrypt')
    assert_equal (data['mergemined'], False)
    assert_equal (data['bits'], '207fffff')
    assert 'difficulty' in data
    fakeHeader = codecs.decode (solved, 'hex_codec')
    fakeHeader = getworkByteswap (fakeHeader)
    fakeHeader = codecs.encode (fakeHeader, 'hex_codec')
    fakeHeader = codecs.decode (fakeHeader, 'ascii')
    assert_equal (data['fakeheader'], fakeHeader)

    # Also previous blocks should have 'powdata', since all blocks (also
    # those generated by "generate") are mined with it.
    oldHash = self.nodes[0].getblockhash (100)
    data = self.nodes[0].getblock (oldHash)
    assert 'powdata' in data

    # Check that it paid correctly to the first node.
    t = self.nodes[0].listtransactions ("*", 1)
    assert_equal (len (t), 1)
    t = t[0]
    assert_equal (t['category'], "immature")
    assert t['generated']
    assert_greater_than_or_equal (t['amount'], Decimal ("1"))
    assert_equal (t['confirmations'], 1)

    # Mine a block using the hash-less form of submit.
    work = create ()
    target = reverseHex (work['target'])
    solved = solveData (work['data'], target, True)
    res = submit (solved)
    assert res
    assert_equal (self.nodes[0].getbestblockhash (), work['hash'])

  def test_getwork (self):
    """
    Test the getwork method.
    """

    create = self.nodes[0].getwork
    submit = self.nodes[0].getwork
    self.test_common (create, submit)

    # Ensure that the payout address is changed from one block to the next.
    hash1 = mineWorkBlockWithMethods (self.nodes[0], create, submit)
    hash2 = mineWorkBlockWithMethods (self.nodes[0], create, submit)
    addr1 = getCoinbaseAddr (self.nodes[0], hash1)
    addr2 = getCoinbaseAddr (self.nodes[0], hash2)
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
    hash1 = mineWorkBlockWithMethods (self.nodes[0], create, submit)
    hash2 = mineWorkBlockWithMethods (self.nodes[0], create, submit)
    addr1 = getCoinbaseAddr (self.nodes[0], hash1)
    addr2 = getCoinbaseAddr (self.nodes[0], hash2)
    assert_equal (addr1, coinbaseAddr)
    assert_equal (addr2, coinbaseAddr)

    # Ensure that different payout addresses will generate different auxblocks
    work1 = self.nodes[0].creatework (self.nodes[0].getnewaddress ())
    work2 = self.nodes[0].creatework (self.nodes[0].getnewaddress ())
    assert work1['hash'] != work2['hash']

    # Test that submitwork works also with just one argument.
    def submitOnlyData (hash, data):
      return self.nodes[0].submitwork (data)
    mineWorkBlockWithMethods (self.nodes[0], create, submitOnlyData)

if __name__ == '__main__':
  AuxpowGetworkTest (__file__).main ()
