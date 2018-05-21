#!/usr/bin/env python3
# Copyright (c) 2018 The Chimaera developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test spendability of premine and that P2SH is enforced correctly for it."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import *
from test_framework.util import *

import codecs

PREMINE_VALUE = Decimal ('222222222')
PREMINE_ADDRESS = 'dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p'
PREMINE_PRIVKEYS = ['b69iyynFSWcU54LqXisbbqZ8uTJ7Dawk3V3yhht6ykxgttqMQFjb',
                    'b3fgAKVQpMj24gbuh6DiXVwCCjCbo1cWiZC2fXgWEU9nXy6sdxD5']

class PremineTest(BitcoinTestFramework):
  def set_test_params(self):
    self.setup_clean_chain = True
    self.num_nodes = 1

  def run_test(self):
    node = self.nodes[0]

    # Find basic data about the genesis coinbase tx.
    genesis = node.getblock (node.getblockhash (0), 2)
    assert_equal (len (genesis['tx']), 1)
    tx = genesis['tx'][0]
    txid = tx['hash']
    assert_equal (len (tx['vout']), 1)
    out = tx['vout'][0]
    assert_equal (out['value'], PREMINE_VALUE)
    assert_equal (out['scriptPubKey']['addresses'], [PREMINE_ADDRESS])

    # The coinbase txout should be in the UTXO set.
    utxo = node.gettxout (txid, 0)
    assert utxo is not None

    # Check balance of node and then import the keys for the premine
    # and check again.  It should be available as spendable.
    assert_equal (node.getbalance (), 0)
    for key in PREMINE_PRIVKEYS:
      node.importprivkey (key, 'premine') 
    pubkeys = []
    for addr in node.getaddressesbylabel ('premine'):
      data = node.getaddressinfo (addr)
      if (not data['isscript']) and (not data['iswitness']):
        pubkeys.append (data['pubkey'])
    p2sh = node.addmultisigaddress (1, pubkeys)
    assert_equal (p2sh['address'], PREMINE_ADDRESS)
    node.rescanblockchain ()
    assert_equal (node.getbalance (), PREMINE_VALUE)

    # Construct a raw tx spending the premine.
    addr = node.getnewaddress ()
    inputs = [{"txid": txid, "vout": 0}]
    outputs = {addr: Decimal ('123456')}
    rawTx = node.createrawtransaction (inputs, outputs)

    # Try to "sign" it by just adding the redeem script, which would have been
    # valid before the P2SH softfork.  Doing so should fail, which verifies that
    # P2SH is enforced right from the start and thus that the premine is safe.
    data = node.getaddressinfo (PREMINE_ADDRESS)
    redeemScript = data['hex']
    # Prepend script size, so that it will correctly push the script hash
    # to the stack.
    redeemScript = ("%02x" % (len (redeemScript) / 2)) + redeemScript
    forgedTx = CTransaction ()
    FromHex (forgedTx, rawTx)
    forgedTx.vin[0].scriptSig = codecs.decode (redeemScript, 'hex_codec')
    forgedTx = ToHex (forgedTx)
    assert_raises_rpc_error (-26, "not valid",
                             node.sendrawtransaction, forgedTx, True)

    # Sign and send the raw tx, should succeed.
    signed = node.signrawtransactionwithwallet (rawTx)
    assert signed['complete']
    signedTx = signed['hex']
    sendId = node.sendrawtransaction (signedTx, True)
    node.generate (1)
    assert_equal (node.gettransaction (sendId)['confirmations'], 1)

if __name__ == '__main__':
  PremineTest().main()
