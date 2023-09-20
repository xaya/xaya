#!/usr/bin/env python3
# Copyright (c) 2023 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests how the RNG seed can be different while the network is in consensus,
# and the fork to fix it.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import (
  create_block,
  create_coinbase,
)
from test_framework.util import assert_equal

FORK_TIME = 1_697_328_000


class XayaRngSeedTest (BitcoinTestFramework):

  def set_test_params (self):
    self.num_nodes = 2
    self.setup_clean_chain = True

  def run_test (self):
    self.log.info ("Before the fork...")
    hash1, hash2 = self.testDifferentPow (FORK_TIME - 100)
    assert_equal (hash1, hash2)
    assert (self.nodes[0].getblock (hash1)["rngseed"]
              != self.nodes[1].getblock (hash2)["rngseed"])

    self.log.info ("After the fork...")
    hash1, hash2 = self.testDifferentPow (FORK_TIME + 100)
    assert_equal (hash1, hash2)
    assert_equal (self.nodes[0].getblock (hash1)["rngseed"],
                  self.nodes[1].getblock (hash2)["rngseed"])

  def testDifferentPow (self, time):
    """
    Mines a block with differing PoW (but otherwise the same) on the
    two nodes we have, and one more block on one node to cause
    both nodes to sync to it.

    Returns the block hash(es) of the differing blocks.
    """

    self.disconnect_nodes (0, 1)
    for n in self.nodes:
      n.setmocktime (time)

    blk = self.createBlock (time)

    blk.solve ()
    self.nodes[0].submitblock (blk.serialize ().hex ())
    hash1 = self.nodes[0].getbestblockhash ()
    def noSync ():
      pass
    self.generate (self.nodes[0], 1, sync_fun=noSync)

    blk.powData.fakeHeader.nNonce = 100
    blk.solve ()
    self.nodes[1].submitblock (blk.serialize ().hex ())
    hash2 = self.nodes[1].getbestblockhash ()

    self.connect_nodes (0, 1)
    assert_equal (self.nodes[0].getbestblockhash (),
                  self.nodes[1].getbestblockhash ())

    return hash1, hash2

  def createBlock (self, time):
    """
    Creates a new block that is valid for the current tip.  Its Pow data
    is not yet solved (just initialised with nBits and basic stuff).
    """

    bestHash = self.nodes[0].getbestblockhash ()
    bestBlock = self.nodes[0].getblock (bestHash)
    tip = int (bestHash, 16)
    height = bestBlock["height"] + 1

    block = create_block (tip, create_coinbase (height), time)

    return block


if __name__ == '__main__':
  XayaRngSeedTest ().main ()
