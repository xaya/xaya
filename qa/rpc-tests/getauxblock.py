#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test the "getauxblock" merge-mining RPC interface.

# Add python-bitcoinrpc to module search path:
import os
import sys
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-bitcoinrpc"))

from bitcoinrpc.authproxy import JSONRPCException
from test_framework import BitcoinTestFramework
from util import assert_equal, Decimal

import auxpow

class GetAuxBlockTest (BitcoinTestFramework):

  def run_test (self):
    BitcoinTestFramework.run_test (self)

    # Generate a block so that we are not "downloading blocks".
    self.nodes[0].setgenerate (True, 1)

    # Compare basic data of getauxblock to getblocktemplate.
    auxblock = self.nodes[0].getauxblock ()
    blocktemplate = self.nodes[0].getblocktemplate ()
    assert_equal (auxblock['coinbasevalue'], blocktemplate['coinbasevalue'])
    assert_equal (auxblock['bits'], blocktemplate['bits'])
    assert_equal (auxblock['height'], blocktemplate['height'])
    assert_equal (auxblock['previousblockhash'], blocktemplate['previousblockhash'])

    # Compare target and take byte order into account.
    target = auxblock['_target']
    reversedTarget = auxpow.reverseHex (target)
    assert_equal (reversedTarget, blocktemplate['target'])

    # Verify data that can be found in another way.
    assert_equal (auxblock['chainid'], 1)
    assert_equal (auxblock['height'], self.nodes[0].getblockcount () + 1)
    assert_equal (auxblock['previousblockhash'], self.nodes[0].getblockhash (auxblock['height'] - 1))

    # Calling again should give the same block.
    auxblock2 = self.nodes[0].getauxblock ()
    assert_equal (auxblock2, auxblock)

    # If we receive a new block, the old hash will be replaced.
    self.sync_all ()
    self.nodes[1].setgenerate (True, 1)
    self.sync_all ()
    auxblock2 = self.nodes[0].getauxblock ()
    assert auxblock['hash'] != auxblock2['hash']
    try:
      self.nodes[0].getauxblock (auxblock['hash'], "x")
      raise AssertionError ("invalid block hash accepted")
    except JSONRPCException as exc:
      assert_equal (exc.error['code'], -8)

    # Invalid format for auxpow.
    try:
      self.nodes[0].getauxblock (auxblock2['hash'], "x")
      raise AssertionError ("malformed auxpow accepted")
    except JSONRPCException as exc:
      assert_equal (exc.error['code'], -1)

    # Invalidate the block again, send a transaction and query for the
    # auxblock to solve that contains the transaction.
    self.nodes[0].setgenerate (True, 1)
    addr = self.nodes[1].getnewaddress ()
    txid = self.nodes[0].sendtoaddress (addr, 1)
    self.sync_all ()
    assert_equal (self.nodes[1].getrawmempool (), [txid])
    auxblock = self.nodes[0].getauxblock ()
    blocktemplate = self.nodes[0].getblocktemplate ()
    target = blocktemplate['target']

    # Compute invalid auxpow.
    apow = auxpow.computeAuxpow (auxblock['hash'], target, False)
    res = self.nodes[0].getauxblock (auxblock['hash'], apow)
    assert not res

    # Compute and submit valid auxpow.
    apow = auxpow.computeAuxpow (auxblock['hash'], target, True)
    res = self.nodes[0].getauxblock (auxblock['hash'], apow)
    assert res

    # Make sure that the block is indeed accepted.
    self.sync_all ()
    assert_equal (self.nodes[1].getrawmempool (), [])
    height = self.nodes[1].getblockcount ()
    assert_equal (height, auxblock['height'])
    assert_equal (self.nodes[1].getblockhash (height), auxblock['hash'])

    # Check that it paid correctly to the first node.
    t = self.nodes[0].listtransactions ("", 1)
    assert_equal (len (t), 1)
    t = t[0]
    assert_equal (t['category'], "immature")
    assert_equal (t['blockhash'], auxblock['hash'])
    assert t['generated']
    assert t['amount'] >= Decimal ("25")
    assert_equal (t['confirmations'], 1)

    # Verify the coinbase script.  Ensure that it includes the block height
    # to make the coinbase tx unique.  The expected block height is around
    # 200, so that the serialisation of the CScriptNum ends in an extra 00.
    # The vector has length 2, which makes up for 02XX00 as the serialised
    # height.  Check this.
    blk = self.nodes[1].getblock (auxblock['hash'])
    tx = self.nodes[1].getrawtransaction (blk['tx'][0], 1)
    coinbase = tx['vin'][0]['coinbase']
    assert_equal ("02%02x00" % auxblock['height'], coinbase[0 : 6])

if __name__ == '__main__':
  GetAuxBlockTest ().main ()
