#!/usr/bin/env python3
# Copyright (c) 2018-2025 The Xaya developers
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
PREMINE_PUBKEYS = [
  '03c278d06b977e67b8ea45ef24e3c96a9258c47bc4cce3d0b497b690d672497b6e',
  '0221ac9dc97fe12a98374344d08b458a9c2c1df9afb29dd6089b94a3b4dc9ad570',
]

class PremineTest(BitcoinTestFramework):
  def set_test_params(self):
    self.setup_clean_chain = True
    self.num_nodes = 1

  def skip_test_if_missing_module (self):
    self.skip_if_no_wallet ()

  def run_test(self):
    node = self.nodes[0]

    # To import the multisig address as watch-only, we need a wallet
    # that has private keys disabled.
    node.createwallet (wallet_name="watchonly", disable_private_keys=True)
    desc = f"addr({PREMINE_ADDRESS})"
    info = node.getdescriptorinfo (desc)
    desc = f"{desc}#{info['checksum']}"
    rpcWatchonly = node.get_wallet_rpc ("watchonly")
    rpcWatchonly.importdescriptors ([{"desc": desc, "timestamp": "now"}])
    rpcWatchonly.rescanblockchain ()
    node.createwallet (wallet_name="privkeys")
    rpcKeys = node.get_wallet_rpc ("privkeys")

    # Find basic data about the genesis coinbase tx.
    genesis = node.getblock (node.getblockhash (0), 2)
    assert_equal (len (genesis['tx']), 1)
    tx = genesis['tx'][0]
    txid = tx['hash']
    assert_equal (len (tx['vout']), 1)
    out = tx['vout'][0]
    assert_equal (out['value'], PREMINE_VALUE)
    assert_equal (out['scriptPubKey']['address'], PREMINE_ADDRESS)

    # Accessing it should work normally (upstream Bitcoin/Namecoin have a
    # special check that disallows the genesis coinbase with getrawtransaction,
    # as it is not spendable).
    rpcWatchonly.gettransaction (txid)
    assert_equal (node.getrawtransaction (txid, False, genesis['hash']),
                  tx['hex'])

    # The coinbase txout should be in the UTXO set.
    utxo = node.gettxout (txid, 0)
    assert utxo is not None

    # Check balance of node and then import the keys for the premine
    # and check again.  It should be available as spendable.
    assert rpcWatchonly.getbalance () > 0
    assert_equal (rpcKeys.getbalance (), 0)
    keys = ",".join (PREMINE_PRIVKEYS)
    desc = f"sh(multi(1,{keys}))"
    info = rpcKeys.getdescriptorinfo (desc)
    desc = f"{desc}#{info['checksum']}"
    [imp] = rpcKeys.importdescriptors ([
      {
        "desc": desc,
        "timestamp": "now",
      }
    ])
    [addr] = rpcKeys.deriveaddresses (desc)
    assert_equal (addr, PREMINE_ADDRESS)
    assert imp["success"]
    pubkeys = rpcKeys.getaddressinfo (PREMINE_ADDRESS)["pubkeys"]
    assert_equal (set (pubkeys), set (PREMINE_PUBKEYS))
    assert_equal (rpcKeys.getbalance (), PREMINE_VALUE)

    # Construct a raw tx spending the premine.
    addr = rpcKeys.getnewaddress ()
    inputs = [{"txid": txid, "vout": 0}]
    outputs = {addr: Decimal ('123456')}
    rawTx = node.createrawtransaction (inputs, outputs)

    # Try to "sign" it by just adding the redeem script, which would have been
    # valid before the P2SH softfork.  Doing so should fail, which verifies that
    # P2SH is enforced right from the start and thus that the premine is safe.
    data = rpcWatchonly.getaddressinfo (PREMINE_ADDRESS)
    redeemScript = data["scriptPubKey"]
    # Prepend script size, so that it will correctly push the script hash
    # to the stack.
    redeemScript = ("%02x" % (len (redeemScript) // 2)) + redeemScript
    forgedTx = tx_from_hex (rawTx)
    forgedTx.vin[0].scriptSig = codecs.decode (redeemScript, 'hex_codec')
    forgedTx = forgedTx.serialize ().hex ()
    assert_raises_rpc_error (-26, "mempool-script-verify-flag-failed",
                             node.sendrawtransaction, forgedTx, 0)

    # Sign and send the raw tx, should succeed.
    signed = rpcKeys.signrawtransactionwithwallet (rawTx)
    assert signed['complete']
    signedTx = signed['hex']
    sendId = node.sendrawtransaction (signedTx, 0)
    self.generate (node, 1)
    assert_equal (rpcWatchonly.gettransaction (sendId)['confirmations'], 1)

if __name__ == '__main__':
  PremineTest(__file__).main()
