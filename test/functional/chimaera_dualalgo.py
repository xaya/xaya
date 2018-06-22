#!/usr/bin/env python3
# Copyright (c) 2018 The Chimaera developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the dual-algo mining."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class DualAlgoTest (BitcoinTestFramework):
  def set_test_params (self):
    self.setup_clean_chain = True
    self.num_nodes = 1

  def assertBlocksNeoscrypt (self, hashes):
    """
    Verifies that the blocks with the given hashes are mined using Neoscrypt,
    including that they are not merge-mined and have a fake header.
    """

    for hash in hashes:
      powData = self.node.getblock (hash)['powdata']
      assert_equal (powData['algo'], 'neoscrypt')
      assert_equal (powData['mergemined'], False)
      assert 'fakeheader' in powData

  def assertBlocksSha256d (self, hashes):
    """
    Verifies that the blocks with the given hashes are mined using SHA256D.

    FIXME: Once we implement merge mining, verify that they are merge mined.
    """

    for hash in hashes:
      powData = self.node.getblock (hash)['powdata']
      assert_equal (powData['algo'], 'sha256d')
      assert_equal (powData['mergemined'], False)
      assert 'fakeheader' in powData

  def run_test (self):
    self.node = self.nodes[0]
    addr = self.node.getnewaddress ()

    # Error for invalid pow algo.
    assert_raises_rpc_error (-8, 'invalid PowAlgo',
                             self.node.generate, 1, None, 'foo')
    assert_raises_rpc_error (-8, 'invalid PowAlgo',
                             self.node.generatetoaddress, 1, addr, None, 'foo')

    # Mine blocks with Neoscrypt and verify that.
    blks = self.node.generate (1, None, 'neoscrypt')
    blks.extend (self.node.generatetoaddress (1, addr, None, 'neoscrypt'))
    blks.extend (self.node.generate (1))
    assert_equal (len (blks), 3)
    self.assertBlocksNeoscrypt (blks)

    # Mine blocks with SHA256D and verify that.
    blks = self.node.generate (1, None, 'sha256d')
    blks.extend (self.node.generatetoaddress (1, addr, None, 'sha256d'))
    assert_equal (len (blks), 2)
    self.assertBlocksSha256d (blks)

if __name__ == '__main__':
  DualAlgoTest ().main ()
