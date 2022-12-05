#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the dual-algo mining."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class DualAlgoTest (BitcoinTestFramework):
  def set_test_params (self):
    self.setup_clean_chain = True
    self.num_nodes = 1

  def skip_test_if_missing_module (self):
    self.skip_if_no_wallet ()

  def add_options (self, parser):
    self.add_wallet_options (parser)

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
    Verifies that the blocks with the given hashes are mined using SHA256D,
    including that they are merge-mined.
    """

    for hash in hashes:
      powData = self.node.getblock (hash)['powdata']
      assert_equal (powData['algo'], 'sha256d')
      assert_equal (powData['mergemined'], True)
      assert 'auxpow' in powData

  def run_test (self):
    self.node = self.nodes[0]
    addr1 = self.node.getnewaddress ()
    addr2 = self.node.getnewaddress ()

    # Error for invalid pow algo.
    assert_raises_rpc_error (-8, 'invalid PowAlgo',
                             self.generatetoaddress,
                             self.node, 1, addr1, None, 'foo')

    # Mine blocks with Neoscrypt and verify that.
    blks = self.generatetoaddress (self.node, 1, addr1, None, 'neoscrypt')
    blks.extend (self.generatetoaddress (self.node, 1, addr1))
    assert_equal (len (blks), 2)
    self.assertBlocksNeoscrypt (blks)

    # Mine blocks with SHA256D and verify that.
    blks = self.generatetoaddress (self.node, 1, addr1, None, 'sha256d')
    assert_equal (len (blks), 1)
    self.assertBlocksSha256d (blks)

    # Verify that Neoscrypt blocks have a higher weight than SHA256 blocks:
    # For this, we create a chain of 10 Neoscrypt blocks.  Then we invalidate
    # it and create an alternate chain of 20 SHA256 blocks.  When we then
    # reconsider the Neoscrypt chain, it should become the current best
    # (and its work should be larger, while its length is smaller than that
    # of the SHA256 chain).
    #
    # Note that we have to create the second chain of blocks to a different
    # coinbase address.  Otherwise they might actually be identical and thus
    # also "invalid"!

    neoscryptBlks = self.generatetoaddress (self.node, 10, addr1,
                                            None, 'neoscrypt')
    neoscryptData = self.node.getblock (neoscryptBlks[-1])
    assert_equal (self.node.getbestblockhash (), neoscryptBlks[-1])

    self.node.invalidateblock (neoscryptBlks[0])
    shaBlks = self.generatetoaddress (self.node, 20, addr2, None, 'sha256d')
    shaData = self.node.getblock (shaBlks[-1])
    assert_equal (self.node.getbestblockhash (), shaBlks[-1])

    self.node.reconsiderblock (neoscryptBlks[0])
    assert_equal (self.node.getbestblockhash (), neoscryptBlks[-1])
    assert_greater_than (neoscryptData['chainwork'], shaData['chainwork'])
    assert_greater_than (shaData['height'], neoscryptData['height'])

    # Check that getblock returns the rngseed and that it is not (in a trivial
    # way) biased towards smaller numbers like the PoW hash itself would be.
    # For this, we verify that at least some seeds are large.
    for algo in ['sha256d', 'neoscrypt']:
      found = False
      for trial in range (100):
        blk = self.generatetoaddress (self.node, 1, addr1, None, algo)[0]
        data = self.node.getblock (blk)
        assert 'rngseed' in data
        if data['rngseed'][0] == 'f':
          found = True
          break
      if not found:
        raise AssertionError ("rng seed was never high for algo %s" % algo)

if __name__ == '__main__':
  DualAlgoTest ().main ()
