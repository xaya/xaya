#!/usr/bin/env python3
# Copyright (c) 2014-2022 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Utility routines for auxpow that are needed specifically by the regtests.
# This is mostly about actually *solving* an auxpow block (with regtest
# difficulty) or inspecting the information for verification.

import binascii
import codecs

from test_framework import auxpow
from test_framework import powhash

def computeAuxpow (block, target, ok):
  """
  Build an auxpow object (serialised as hex string) that solves
  (ok = True) or doesn't solve (ok = False) the block.
  """

  (tx, header) = auxpow.constructAuxpow (block)
  (header, _) = mineBlock (header, target, ok)
  return auxpow.finishAuxpow (tx, header)

def mineAuxpowBlock (node, addr):
  """
  Mine an auxpow block on the given RPC connection.  This uses the
  createauxblock and submitauxblock command pair.
  """

  def create ():
    return node.createauxblock (addr)

  return mineAuxpowBlockWithMethods (create, node.submitauxblock)

def mineAuxpowBlockWithMethods (rpc, create, submit):
  """
  Mine an auxpow block, using the given methods for creation and submission.
  """

  auxblock = create ()
  target = auxpow.reverseHex (auxblock['_target'])
  apow = computeAuxpow (auxblock['hash'], target, True)
  res = submit (auxblock['hash'], apow)
  assert res

  return rpc.getbestblockhash ()

def mineWorkBlockWithMethods (rpc, create, submit):
  """
  Mine a stand-alone block, using the given methods for creation and submission.
  """

  work = create ()
  target = auxpow.reverseHex (work['target'])
  solved = solveData (work['data'], target, True)
  res = submit (work['hash'], solved)
  assert res

  return rpc.getbestblockhash ()

def getCoinbaseAddr (node, blockHash):
    """
    Extract the coinbase tx' payout address for the given block.
    """

    blockData = node.getblock (blockHash)
    txn = blockData['tx']
    assert len (txn) >= 1

    txData = node.getrawtransaction (txn[0], True, blockHash)
    assert len (txData['vout']) >= 1 and len (txData['vin']) == 1
    assert 'coinbase' in txData['vin'][0]

    return txData['vout'][0]['scriptPubKey']['address']

def mineBlock (header, target, ok, hashFcn=auxpow.doubleHashHex):
  """
  Given a block header, update the nonce until it is ok (or not)
  for the given target.
  """

  data = bytearray (binascii.unhexlify (header))
  while True:
    assert data[79] < 255
    data[79] += 1
    hexData = binascii.hexlify (data)

    blockhash = hashFcn (hexData)
    if (ok and blockhash < target) or ((not ok) and blockhash > target):
      break

  return (hexData, blockhash)

def solveData (hexData, target, ok):
  """
  Solve a block header given as hex in getwork's 'data' format (or not).  This
  uses Neoscrypt for hashing, since that is what we use in Xaya for
  stand-alone (getwork) blocks.
  """

  data = codecs.decode (hexData, 'hex_codec')
  data = auxpow.getworkByteswap (data[:80])

  def neoscrypt (hexStr):
    rawData = codecs.decode (hexStr, 'hex_codec')
    rawHash = bytearray (powhash.forHeader ('neoscrypt', rawData))
    rawHash.reverse ()
    return codecs.encode (rawHash, 'hex_codec')

  hexSolved, h = mineBlock (codecs.encode (data, 'hex_codec'), target, ok,
                            hashFcn=neoscrypt)
  solved = codecs.decode (hexSolved, 'hex_codec')

  solved = auxpow.getworkByteswap (solved)
  return codecs.decode (codecs.encode (solved, 'hex_codec'), 'ascii')
