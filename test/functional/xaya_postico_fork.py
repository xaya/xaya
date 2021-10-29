#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Tests the POST_ICO hard fork in Xaya."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import (
  create_block,
  create_coinbase,
)
from test_framework.util import (
  assert_equal,
  assert_greater_than,
)

FORK_HEIGHT = 500

class PostIcoForkTest (BitcoinTestFramework):
  def set_test_params (self):
    self.setup_clean_chain = True
    self.num_nodes = 1

  def run_test (self):
    self.node = self.nodes[0]

    # Test that non-zero nonce values are fine at the fork.
    height = self.node.getblockcount ()
    assert_greater_than (FORK_HEIGHT - 1, height)
    self.generate (self.node, FORK_HEIGHT - 1 - height)
    assert_equal (FORK_HEIGHT - 1, self.node.getblockcount ())
    assert_equal (self.tryNonzeroNonceBlock (), None)
    blkHash = self.node.getbestblockhash ()
    assert_equal (FORK_HEIGHT, self.node.getblockcount ())
    assert_equal (42, self.node.getblock (blkHash)['nonce'])
    assert_equal (42, self.node.getblockheader (blkHash)['nonce'])

    # The fork also changes the block reward as well as the block intervals
    # (depending on the algorithm), but that is nothing that can be easily
    # tested in regtest mode.  Thus we rely on unit tests and testnet for
    # verifying those changes.

    # Test that we can restart the client just fine.  There was a bug
    # with non-zero nNonce values in the on-disk block index that caused
    # a crash here:  https://github.com/xaya/xaya/issues/82
    self.generate (self.node, 10)
    self.restart_node (0)
    assert_equal (blkHash, self.nodes[0].getblockhash (FORK_HEIGHT))
    assert_equal (FORK_HEIGHT + 10, self.nodes[0].getblockcount ())
    assert_equal (42, self.node.getblock (blkHash)['nonce'])
    assert_equal (42, self.node.getblockheader (blkHash)['nonce'])

  def tryNonzeroNonceBlock (self):
    """
    Creates and mines a block with nonzero nonce and tries to submit it
    to self.node.
    """

    tip = self.node.getbestblockhash ()
    height = self.node.getblockcount () + 1
    time = self.node.getblockheader (tip)["mediantime"] + 1
    block = create_block (int (tip, 16), create_coinbase (height), time,
                          version=4)
    block.nNonce = 42

    block.solve ()
    return self.node.submitblock (block.serialize (True).hex ())

if __name__ == '__main__':
  PostIcoForkTest ().main ()
