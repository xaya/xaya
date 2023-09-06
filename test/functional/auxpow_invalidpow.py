#!/usr/bin/env python3
# Copyright (c) 2019-2021 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests what happens if we submit a valid block with invalid PoW data.
# Obviously the block should not be accepted, but it should also not be
# marked as permanently invalid.  So resubmitting the same block with a
# valid PoW data should then work fine.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import (
  create_block,
  create_coinbase,
)
from test_framework.messages import uint256_from_compact
from test_framework.p2p import P2PDataStore
from test_framework.util import assert_equal

from test_framework.auxpow_testing import computeAuxpow

import codecs
from io import BytesIO

class AuxpowInvalidPoWTest (BitcoinTestFramework):

  def set_test_params (self):
    self.num_nodes = 1
    self.setup_clean_chain = True
    self.extra_args = [["-whitelist=127.0.0.1"]]

  def run_test (self):
    node = self.nodes[0]
    peer = node.add_p2p_connection (P2PDataStore ())

    self.log.info ("Sending block with invalid auxpow over P2P...")
    tip = node.getbestblockhash ()
    blk = self.createBlock ()
    blkHash = self.solvePowData (blk, False)
    print (blkHash)
    peer.send_blocks_and_test ([blk], node, force_send=True,
                               success=False, reject_reason="high-hash")
    assert_equal (node.getbestblockhash (), tip)

    self.log.info ("Sending the same block with valid auxpow...")
    blkHash = self.solvePowData (blk, True)
    print (blkHash)
    peer.send_blocks_and_test ([blk], node, success=True)
    assert_equal (node.getbestblockhash (), blkHash)

    self.log.info ("Submitting block with invalid auxpow...")
    tip = node.getbestblockhash ()
    blk = self.createBlock ()
    blkHash = self.solvePowData (blk, False)
    assert_equal (node.submitblock (blk.serialize ().hex ()), "high-hash")
    assert_equal (node.getbestblockhash (), tip)

    self.log.info ("Submitting block with valid auxpow...")
    blkHash = self.solvePowData (blk, True)
    assert_equal (node.submitblock (blk.serialize ().hex ()), None)
    assert_equal (node.getbestblockhash (), blkHash)

  def createBlock (self):
    """
    Creates a new block that is valid for the current tip.  Its Pow data
    is not yet solved (just initialised with nBits and basic stuff).
    """

    bestHash = self.nodes[0].getbestblockhash ()
    bestBlock = self.nodes[0].getblock (bestHash)
    tip = int (bestHash, 16)
    height = bestBlock["height"] + 1
    time = bestBlock["time"] + 1

    block = create_block (tip, create_coinbase (height), time)

    block.powData.fakeHeader.hashMerkleRoot = block.baseHash
    block.powData.rehash ()

    return block


  def solvePowData (self, block, ok):
    """
    Solves the fake header in the given powData, to be either valid
    (ok = True) or invalid (ok = False).
    """

    target = uint256_from_compact (block.powData.nBits)

    while True:
      block.powData.fakeHeader.nNonce += 1
      block.powData.rehash ()
      isOk = (block.powData.fakeHeader.powHash <= target)
      if isOk == ok:
        block.rehash ()
        return "%064x" % block.sha256


if __name__ == '__main__':
  AuxpowInvalidPoWTest ().main ()
