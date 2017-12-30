#!/usr/bin/env python

#   Auxpow Sizes - find sizes of auxpow's in the blockchain
#   Copyright (C) 2015  Daniel Kraft <d@domob.eu>
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU Affero General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU Affero General Public License for more details.
#
#   You should have received a copy of the GNU Affero General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

import jsonrpc
import sys
import urllib

username = urllib.quote_plus ("namecoin")
password = urllib.quote_plus ("password")
port = 8336
url = "http://%s:%s@localhost:%d/" % (username, password, port)

class AuxpowStats:
  """
  Keep track of the interesting statistics of the auxpows
  found in the blockchain.
  """

  def __init__ (self):
    self.merkleLength = dict ()
    self.txSize = dict ()
    self.maxMerkle = 0
    self.maxTxSize = 0

  def add (self, obj):
    """
    Add the auxpow described by the block JSON obj (if any)
    to the statistics.
    """

    if 'auxpow' not in obj:
      return

    txSize = len (obj['auxpow']['tx']['hex']) / 2
    merkleLen = len (obj['auxpow']['merklebranch'])

    if txSize not in self.txSize:
      self.txSize[txSize] = 1
    else:
      self.txSize[txSize] += 1

    if merkleLen not in self.merkleLength:
      self.merkleLength[merkleLen] = 1
    else:
      self.merkleLength[merkleLen] += 1

    if txSize > self.maxTxSize:
      self.maxTxSize = txSize
      self.maxTxSizeHash = obj['hash']
    if merkleLen > self.maxMerkle:
      self.maxMerkle = merkleLen
      self.maxMerkleHash = obj['hash']

  def output (self):
    """
    Output statistics in the end.
    """

    print "Merkle lengths:"
    for (key, val) in self.merkleLength.items ():
      print "%4d: %6d" % (key, val)
    print "Maximum: %d, block %s\n" % (self.maxMerkle, self.maxMerkleHash)

    print "\nCoinbase tx sizes:"
    buckets = [0, 1000, 2000, 5000, 10000, 20000, 50000]
    bucketCnts = (len (buckets) + 1) * [0]
    for (key, val) in self.txSize.items ():
      for i in range (len (buckets) - 1, -1, -1):
        if (key >= buckets[i]):
          bucketCnts[i] += val
    for i in range (len (buckets) - 1):
      label = "%d - %d" % (buckets[i], buckets[i + 1] - 1)
      print "  %15s: %6d" % (label, bucketCnts[i])
    label = ">= %d" % buckets[-1]
    print "  %15s: %6d" % (label, bucketCnts[-1])
    print "Maximum: %d, block %s\n" % (self.maxTxSize, self.maxTxSizeHash)

rpc = jsonrpc.proxy.ServiceProxy (url)
tips = rpc.getchaintips ()
tip = None
for t in tips:
  if t['status'] == 'active':
    tip = t
    break
assert tip is not None

stats = AuxpowStats ()
curHash = tip['hash']
while True:
  obj = rpc.getblock (curHash)
  stats.add (obj)
  if obj['height'] % 1000 == 0:
    sys.stderr.write ("At height %d...\n" % obj['height'])
  if 'previousblockhash' not in obj:
    break
  curHash = obj['previousblockhash']
stats.output ()
