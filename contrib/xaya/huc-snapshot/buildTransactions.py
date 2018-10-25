#!/usr/bin/env python
# Copyright (c) 2018 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Script that reads the processed snapshot data (produced by processSnapshot.py)
# and builds up a chain of transactions starting from one output that is large
# enough to cover the full amount as well as some fees.
#
# The transactions are built and written to disk rather than submitted
# right away, so that everything can be processed, verified and only
# then sent.
#
# The URL at which Xaya Core's JSON-RPC interface is available should be
# passed as CLI argument (including the credentials and wallet name
# if applicable), e.g.
#
#   http://user:password@localhost:8396/wallet/walletname

from decimal import Decimal
import glob
import json
import jsonrpclib
import logging
import os
import sys

PRECISION = Decimal ('1.00000000')

INPUT_TXID = "0b1443ed7dc871add11bc08cdb0bd9f967925671cb1b1f8b9ec35c03415995b0"
INPUT_VOUT = 0

PAYMENTS_PER_TX = 100
FEE_PER_TX = Decimal ('0.01000000')

logging.basicConfig (level=logging.INFO, stream=sys.stderr)
log = logging.getLogger ()

if len (sys.argv) != 3:
  sys.exit ("Usage: buildTransactions.py DATA-FILE JSON-RPC-URL")

dataFile = sys.argv[1]
rpcUrl = sys.argv[2]

with open (dataFile) as f:
  snapshot = json.load (f)

xaya = jsonrpclib.Server (rpcUrl)

# Check that the wallet is unlocked.
info = xaya.getwalletinfo ()
if 'unlocked_until' in info and info['unlocked_until'] < 1000000000:
  sys.exit ("The Xaya Core wallet must be unlocked")

# Query the original CHI output that will be spent.
originalInput = xaya.gettxout (INPUT_TXID, INPUT_VOUT)
inputValue = Decimal (originalInput['value']).quantize (PRECISION)

# Extract the address to be used for intermediate change and verify that
# it is owned by the wallet.
assert len (originalInput['scriptPubKey']['addresses']) == 1
myAddress = originalInput['scriptPubKey']['addresses'][0]
log.info ("Intermediate change will be sent to: %s" % myAddress)
info = xaya.getaddressinfo (myAddress)
assert info['ismine']
myScriptPubKey = info['scriptPubKey']

# Modify the data for the tx input so that it is accepted as
# "previous output" for signrawtransactionwithwallet.
originalInput['amount'] = float (inputValue)
originalInput['txid'] = INPUT_TXID
originalInput['vout'] = INPUT_VOUT
originalInput['scriptPubKey'] = originalInput['scriptPubKey']['hex']

# Quick check on the amount compared to what we need to give out.
requiredChi = Decimal ('0.00000000')
for entry in snapshot:
  requiredChi += Decimal (entry['amount']['chi']).quantize (PRECISION)
assert requiredChi < inputValue
totalFee = inputValue - requiredChi
log.info ("CHI sent out in snapshot: %s" % requiredChi)
log.info ("Value of input: %s" % inputValue)
log.info ("Available for fees: %s" % totalFee)

# Helper function that builds a signed raw transaction spending the given
# input, paying to the given destinations and returning the change output.
def buildTx (inp, destinations):
  outputAmount = Decimal ('0.00000000')
  for _, value in destinations.iteritems ():
    outputAmount += Decimal (value).quantize (PRECISION)
  assert outputAmount < inp['amount']

  outs = destinations
  change = Decimal (inp['amount']).quantize (PRECISION)
  change -= outputAmount
  change -= FEE_PER_TX
  outs[myAddress] = float (change)

  tx = xaya.createrawtransaction ([inp], outs)
  tx = xaya.signrawtransactionwithwallet (tx, [inp])
  assert tx['complete']

  decoded = xaya.decoderawtransaction (tx['hex'])
  vout = None
  for ind, out in enumerate (decoded['vout']):
    if out['scriptPubKey']['hex'] == myScriptPubKey:
      vout = ind
      break
  assert vout is not None

  log.info ("Created %s (size %d bytes); remaining value: %s"
              % (decoded['txid'], len (tx['hex']) // 2, change))

  changeOut = {
    "txid": decoded['txid'],
    "vout": vout,
    "amount": float (change),
    "scriptPubKey": myScriptPubKey,
  }

  return tx['hex'], changeOut

# Build up the chain of transactions.
txs = []
dests = {}
prevOut = originalInput
for entry in snapshot:
  if len (dests) == PAYMENTS_PER_TX:
    tx, prevOut = buildTx (prevOut, dests)
    txs.append (tx)
    dests = {}
  dests[entry['address']['chi']] = entry['amount']['chi']
tx, _ = buildTx (prevOut, dests)
txs.append (tx)

# Verify that the set of payments made in the transactions is exactly the
# set of payments that should be made for the snapshot data.  This is a
# final sanity check that everything is fine.
expectedPayments = {}
for entry in snapshot:
  addr = entry['address']['chi']
  assert addr not in expectedPayments
  expectedPayments[addr] = Decimal (entry['amount']['chi']).quantize (PRECISION)
assert len (expectedPayments) == len (snapshot)

actualPayments = {}
for tx in txs:
  data = xaya.decoderawtransaction (tx)
  foundChange = False
  for out in data['vout']:
    if out['scriptPubKey']['hex'] == myScriptPubKey:
      assert not foundChange
      foundChange = True
      continue
    assert len (out['scriptPubKey']['addresses']) == 1
    addr = out['scriptPubKey']['addresses'][0]
    assert addr not in actualPayments
    actualPayments[addr] = Decimal (out['value']).quantize (PRECISION)
  assert foundChange

assert expectedPayments == actualPayments
log.info ("Sanity check of created transactions is good")

# Clean up the tx directory.
log.info ("Cleaning up previously-safed transactions")
for filename in glob.glob ("tx/*.hex"):
  os.remove (filename)

# Save all transactions to the tx directory.
for ind, tx in enumerate (txs):
  data = xaya.decoderawtransaction (tx)
  filename = "tx/%02d_%s.hex" % ((ind + 1), data['txid'])
  with open (filename, "w") as f:
    f.write (tx)
log.info ("All transactions have been written to disk")
