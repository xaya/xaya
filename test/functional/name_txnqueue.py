#!/usr/bin/env python3
# Licensed under CC0 (Public domain)

# Test that transaction queue works.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameTransactionQueueTest(NameTestFramework):
    def set_test_params(self):
        self.setup_name_test ([[]] * 1)
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0] # alias
        self.generate (node, 200) # get out of IBD

        self.log.info("Register a name.")
        addr = node.getnewaddress()
        txid = node.name_register("d/name", val("first"), {"destAddress": addr})
        newTx = node.getrawtransaction(txid)
        nameInd = self.rawtxOutputIndex(0, newTx, addr)

        self.log.info("Mine that transaction.")
        self.generate (node, 1)

        self.log.info("Create a NAME_UPDATE with sequence=12...")
        nameAmount = Decimal('0.01')
        ins        = [{"txid": txid, "vout": nameInd, "sequence": 13}] # 12 blocks
        txRaw      = node.createrawtransaction(ins, {addr: nameAmount})
        op         = {"op": "name_update", "name": "d/name",
                      "value": val("second")}
        txName     = node.namerawtransaction(txRaw, 0, op)['hex']
        txFunded   = node.fundrawtransaction(txName)['hex']
        signed     = node.signrawtransactionwithwallet(txFunded)
        assert signed['complete']
        signedHex  = signed['hex']

        self.log.info("Enqueue it.")
        txid = node.queuerawtransaction(signedHex)

        self.log.info("Make sure it's there.")
        assert txid in node.listqueuedtransactions()

        self.log.info("Take it out.")
        node.dequeuetransaction(txid)

        self.log.info("Make sure it's gone.")
        assert txid not in node.listqueuedtransactions()

        self.log.info("Put it back in again.")
        txid = node.queuerawtransaction(signedHex)
        assert txid in node.listqueuedtransactions()

        self.log.info("Wait 11 blocks...")
        self.generate (node, 11)

        self.log.info("Make sure it hasn't been broadcast yet.")
        assert txid in node.listqueuedtransactions()
        self.checkName(0, "d/name", val("first"))

        self.log.info("Wait one block more.")
        self.generate (node, 1)

        self.log.info("Make sure it's been dequeued.")
        assert txid not in node.listqueuedtransactions()

        self.log.info("Wait one block for the transaction to be confirmed.")
        self.generate (node, 1)

        self.log.info("Check name is updated.")
        self.checkName(0, "d/name", val("second"))

        self.log.info("OK!")

        self.log.info("Queue some garbage.")
        # this parses, but is not valid
        assert_raises_rpc_error(-4, "Invalid transaction (bad-txns-vin-empty)", node.queuerawtransaction, "01000000000000000000")

        self.log.info("Make a valid transaction, check it's instantly broadcast.")
        dummy_addr     = node.getnewaddress()
        dummy_txRaw    = node.createrawtransaction([], {dummy_addr: Decimal("1")})
        dummy_txFunded = node.fundrawtransaction(dummy_txRaw)['hex']
        dummy_txSigned = node.signrawtransactionwithwallet(dummy_txFunded)['hex']

        self.log.info("Put it in.")
        dummy_txid = node.queuerawtransaction(dummy_txSigned)
        self.log.info("Make sure it's already gone.")
        assert dummy_txid not in node.listqueuedtransactions()
        self.log.info("Make sure it's in the mempool.")
        assert dummy_txid in node.getrawmempool()

if __name__ == '__main__':
    NameTransactionQueueTest ().main ()
