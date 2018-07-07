#!/usr/bin/env python3
# Copyright (c) 2014-2018 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# General code for auxpow testing.  This includes routines to
# solve an auxpow and to generate auxpow blocks.

import binascii
import codecs
import hashlib

from test_framework import powhash

def constructAuxpow (block):
  """
  Starts to construct a minimal auxpow, ready to be mined.  Returns the fake
  coinbase tx and the unmined parent block header as hex strings.
  """

  block = codecs.encode (block, 'ascii')

  # Start by building the merge-mining coinbase.  The merkle tree
  # consists only of the block hash as root.
  coinbase = b"fabe" + binascii.hexlify (b"m" * 2)
  coinbase += block
  coinbase += b"01000000" + (b"00" * 4)

  # Construct "vector" of transaction inputs.
  vin = b"01"
  vin += (b"00" * 32) + (b"ff" * 4)
  vin += codecs.encode ("%02x" % (len (coinbase) // 2), "ascii") + coinbase
  vin += (b"ff" * 4)

  # Build up the full coinbase transaction.  It consists only
  # of the input and has no outputs.
  tx = b"01000000" + vin + b"00" + (b"00" * 4)
  txHash = doubleHashHex (tx)

  # Construct the parent block header.  It need not be valid, just good
  # enough for auxpow purposes.
  header = b"01000000"
  header += b"00" * 32
  header += reverseHex (txHash)
  header += b"00" * 4
  header += b"00" * 4
  header += b"00" * 4

  return (tx.decode ('ascii'), header.decode ('ascii'))

def finishAuxpow (tx, header):
  """
  Constructs the finished auxpow hex string based on the mined header.
  """

  blockhash = doubleHashHex (header)

  # Build the MerkleTx part of the auxpow.
  auxpow = codecs.encode (tx, 'ascii')
  auxpow += blockhash
  auxpow += b"00"
  auxpow += b"00" * 4

  # Extend to full auxpow.
  auxpow += b"00"
  auxpow += b"00" * 4
  auxpow += header

  return auxpow.decode ("ascii")

def computeAuxpow (block, target, ok):
  """
  Build an auxpow object (serialised as hex string) that solves
  (ok = True) or doesn't solve (ok = False) the block.
  """

  (tx, header) = constructAuxpow (block)
  (header, _) = mineBlock (header, target, ok)
  return finishAuxpow (tx, header)

def mineAuxpowBlock (node):
  """
  Mine an auxpow block on the given RPC connection.  This uses the
  createauxblock and submitauxblock command pair.
  """

  def create ():
    addr = node.getnewaddress ()
    return node.createauxblock (addr)

  return mineAuxpowBlockWithMethods (create, node.submitauxblock)

def mineAuxpowBlockWithMethods (create, submit):
  """
  Mine an auxpow block, using the given methods for creation and submission.
  """

  auxblock = create ()
  target = reverseHex (auxblock['_target'])
  apow = computeAuxpow (auxblock['hash'], target, True)
  res = submit (auxblock['hash'], apow)
  assert res

  return auxblock['hash']

def mineWorkBlockWithMethods (rpc, create, submit):
  """
  Mine a stand-alone block, using the given methods for creation and submission.
  """

  work = create ()
  target = reverseHex (work['target'])
  solved = solveData (work['data'], target, True)
  res = submit (work['hash'], solved)
  assert res

  return work['hash']

def getCoinbaseAddr (node, blockHash):
    """
    Extract the coinbase tx' payout address for the given block.
    """

    blockData = node.getblock (blockHash)
    txn = blockData['tx']
    assert len (txn) >= 1

    txData = node.getrawtransaction (txn[0], 1)
    assert len (txData['vout']) >= 1 and len (txData['vin']) == 1
    assert 'coinbase' in txData['vin'][0]

    addr = txData['vout'][0]['scriptPubKey']['addresses']
    assert len (addr) == 1
    return addr[0]

def doubleHashHex (data):
  """
  Perform Bitcoin's Double-SHA256 hash on the given hex string.
  """

  hasher = hashlib.sha256 ()
  hasher.update (binascii.unhexlify (data))
  data = hasher.digest ()

  hasher = hashlib.sha256 ()
  hasher.update (data)

  return reverseHex (hasher.hexdigest ())

def mineBlock (header, target, ok, hashFcn=doubleHashHex):
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

def reverseHex (data):
  """
  Flip byte order in the given data (hex string).
  """

  b = bytearray (binascii.unhexlify (data))
  b.reverse ()

  return binascii.hexlify (b)

def getworkByteswap (data):
  """
  Run the byte-order swapping step necessary for working with getwork.
  """

  data = bytearray (data)
  assert len (data) % 4 == 0
  for i in range (0, len (data), 4):
    data[i], data[i + 3] = data[i + 3], data[i]
    data[i + 1], data[i + 2] = data[i + 2], data[i + 1]

  return data

def solveData (hexData, target, ok):
  """
  Solve a block header given as hex in getwork's 'data' format (or not).  This
  uses Neoscrypt for hashing, since that is what we use in Xaya for
  stand-alone (getwork) blocks.
  """

  data = codecs.decode (hexData, 'hex_codec')
  data = getworkByteswap (data[:80])

  def neoscrypt (hexStr):
    rawData = codecs.decode (hexStr, 'hex_codec')
    rawHash = bytearray (powhash.forHeader ('neoscrypt', rawData))
    rawHash.reverse ()
    return codecs.encode (rawHash, 'hex_codec')

  hexSolved, h = mineBlock (codecs.encode (data, 'hex_codec'), target, ok,
                            hashFcn=neoscrypt)
  solved = codecs.decode (hexSolved, 'hex_codec')

  solved = getworkByteswap (solved)
  return codecs.decode (codecs.encode (solved, 'hex_codec'), 'ascii')
