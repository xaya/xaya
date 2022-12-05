#!/usr/bin/env python3
# Copyright (c) 2019-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Tests trading with atomic name updates."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import (
  COIN,
  COutPoint,
  CTransaction,
  CTxIn,
  CTxOut,
)
from test_framework.script import (
  CScript,
  OP_2DROP,
  OP_DROP,
  OP_NAME_UPDATE,
)
from test_framework.util import (
  assert_equal,
  assert_greater_than,
)

from decimal import Decimal
import io
import json


# The fee paid for example transactions.
FEE = Decimal ('0.01')


class AtomicTradingTest (BitcoinTestFramework):
  def set_test_params (self):
    self.setup_clean_chain = True
    self.num_nodes = 2

  def skip_test_if_missing_module (self):
    self.skip_if_no_wallet ()

  def add_options (self, parser):
    self.add_wallet_options (parser)

  def generate (self, n, ind = 0):
    """
    Mines n blocks with rewards sent to an address that is in the wallet
    of none of the test nodes.  This ensures that balances are stable and
    not changing except through the test.
    """

    addr = "chirt1qcmdxwpu35mqlzxz3alc9u9ztp22edsuc5s7zzk"
    self.generatetoaddress (self.nodes[ind], n, addr)

  def buildTxOut (self, addr, amount):
    """
    Builds a CTxOut message that sends the given amount of CHI to the
    given address.
    """

    addrData = self.nodes[0].validateaddress (addr)
    addrScript = bytes.fromhex (addrData["scriptPubKey"])

    return CTxOut (int (amount * COIN), addrScript)

  def buildNameUpdate (self, name, value, addr, amount):
    """
    Builds a name_update output with the given data.
    """

    addrData = self.nodes[0].validateaddress (addr)
    addrScript = bytes.fromhex (addrData["scriptPubKey"])

    bname = name.encode ("utf-8")
    bvalue = value.encode ("utf-8")
    nameScript = CScript ([OP_NAME_UPDATE, bname, bvalue, OP_2DROP, OP_DROP])

    # Adding two CScript instances together pushes the second operand
    # as data, rather than simply concatenating the scripts.  Thus we do
    # the concatenation as raw bytes.
    nameScriptBytes = bytes (nameScript)

    return CTxOut (int (amount * COIN), nameScriptBytes + addrScript)

  def findOutput (self, node, amount):
    """
    Finds an unspent output in the given node with at least the required
    amount.  Returns the matching COutPoint as well as its value.
    """

    for u in node.listunspent ():
      if u["amount"] >= amount:
        outp = COutPoint (int (u["txid"], 16), u["vout"])
        return outp, Decimal (u["amount"])

    raise AssertionError ("No output found with value >= %.8f" % amount)

  def parseHexTx (self, txHex):
    """
    Converts a transaction in hex format to a CTransaction instance.
    """

    data = bytes.fromhex (txHex)

    tx = CTransaction ()
    tx.deserialize (io.BytesIO (data))

    return tx

  def getBalances (self):
    """
    Returns an array with the balances of both nodes.
    """

    return [self.nodes[i].getbalance () for i in range (2)]

  def assertBalanceChange (self, before, changes):
    """
    Asserts that the balances of the nodes have changed compared to
    the values of "before" in the given amount.
    """

    after = self.getBalances ()
    assert_equal (len (before), len (changes))
    assert_equal (after, [before[i] + changes[i] for i in range (len (before))])

  def getTxFee (self, node, txid):
    """
    Computes the paid transaction fee in the given tx.  All inputs to the
    transaction must be in the node's wallet.
    """

    txHex = node.gettransaction (txid)["hex"]
    data = node.decoderawtransaction (txHex)

    inSum = Decimal ('0.00000000')
    for vin in data["vin"]:
      prevTxHex = node.gettransaction (vin["txid"])["hex"]
      prevTx = node.decoderawtransaction (prevTxHex)
      inSum += Decimal (prevTx["vout"][vin["vout"]]["value"])

    outSum = Decimal ('0.00000000')
    for vout in data["vout"]:
      outSum += Decimal (vout["value"])

    assert_greater_than (inSum, outSum)
    return inSum - outSum

  def buildBid (self, node, name, value, price):
    """
    Builds a partially signed "bid" offer for updating the name to the
    given value and paying the given price for that.  The node is used as
    the bidder (i.e. the price is funded from it).

    The partially signed bid transaction is returned as hex string.
    """

    nameData = node.name_show (name)
    addr = nameData["address"]
    namePrevOut = node.gettxout (nameData["txid"], nameData["vout"])
    assert_equal (namePrevOut["scriptPubKey"]["address"], addr)
    nameValue = namePrevOut["value"]

    tx = CTransaction ()
    nameOut = COutPoint (int (nameData["txid"], 16), nameData["vout"])
    tx.vin.append (CTxIn (nameOut))
    tx.vout.append (self.buildNameUpdate (name, value, addr, nameValue))
    tx.vout.append (self.buildTxOut (addr, price))

    inp, inValue = self.findOutput (node, price)
    tx.vin.append (CTxIn (inp))

    change = inValue - price - FEE
    assert_greater_than (change, 0)
    changeAddr = node.getnewaddress ()
    tx.vout.append (self.buildTxOut (changeAddr, change))

    txHex = tx.serialize ().hex ()

    signed = node.signrawtransactionwithwallet (txHex)
    assert not signed["complete"]
    return signed["hex"]

  def buildAsk (self, node, name, value, price):
    """
    Builds a partially signed "ask" offer for updating the name as given.
    The problem with prebuilt asks is that the seller does not know
    which inputs the buyer uses to pay.  This is solved by signing the
    name input with SINGLE|ANYONECANPAY and sending the ask price
    *into the name*.  (It can be recovered later, as the only requirement
    for the locked amount is that it always stays >= 0.01 CHI.)
    The node is the seller, who owns the name.

    Note that this type of order is rather useless for most real-world
    situations of trading game assets (since the name value would need to
    contain a transfer of assets to the seller, which is not known yet).
    There may still be some situations where it can be useful, but it is
    mainly interesting since the same method can be applied for
    "sentinel inputs" as well; the only difference there is that the
    input/output pair created does not involve any names at all.
    """

    nameData = node.name_show (name)
    namePrevOut = node.gettxout (nameData["txid"], nameData["vout"])
    nameValue = namePrevOut["value"]
    addr = node.getnewaddress ()

    tx = CTransaction ()
    nameOut = COutPoint (int (nameData["txid"], 16), nameData["vout"])
    tx.vin.append (CTxIn (nameOut))
    tx.vout.append (self.buildNameUpdate (name, value, addr, nameValue + price))

    txHex = tx.serialize ().hex ()

    signed = node.signrawtransactionwithwallet (txHex, [],
                                                "SINGLE|ANYONECANPAY")
    assert signed["complete"]
    return signed["hex"]

  def run_test (self):
    # Mine initial blocks so that both nodes have matured coins and no
    # more are mined for them in the future (so we can check balances).
    super ().generate (self.nodes[0], 10)
    super ().generate (self.nodes[1], 10)
    self.generate (110, ind=0)

    # Register a name for testing.
    self.nodes[0].name_register ("p/test", "{}")
    self.generate (1, ind=0)

    # Make sure everything is as expected.
    self.sync_blocks ()
    for node in self.nodes:
      info = node.getwalletinfo ()
      assert_equal (info["immature_balance"], 0)

    # Run individual tests.
    self.testBidOffer ()
    self.testAskOffer ()

  def testBidOffer (self):
    self.log.info ("Testing trading by taking a bid offer...")

    # Build the bid transaction.
    name = "p/test"
    newValue = json.dumps ({"data": "bid taken"})
    bid = self.buildBid (self.nodes[1], name, newValue, 10)

    # The seller must not change the name-update value (this will invalidate
    # the signature on the bid).
    wrongValue = json.dumps ({"data": "wrong"})
    addr = self.nodes[0].getnewaddress ()
    tx = self.parseHexTx (bid)
    tx.vout[0] = self.buildNameUpdate (name, wrongValue, addr, 0.01)
    txHex = tx.serialize ().hex ()
    signed = self.nodes[0].signrawtransactionwithwallet (txHex)
    assert not signed["complete"]

    # The seller also must not change the amount he gets.
    tx = self.parseHexTx (bid)
    tx.vout[1].nValue = 20 * COIN
    txHex = tx.serialize ().hex ()
    signed = self.nodes[0].signrawtransactionwithwallet (txHex)
    assert not signed["complete"]

    # Take the bid successfully and verify the expected changes.
    signed = self.nodes[0].signrawtransactionwithwallet (bid)
    assert signed["complete"]
    oldValue = self.nodes[0].name_show (name)["value"]
    assert oldValue != newValue
    before = self.getBalances ()
    self.nodes[0].sendrawtransaction (signed["hex"])
    self.generate (1)
    self.sync_blocks ()
    self.assertBalanceChange (before, [10, -10 - FEE])
    nameData = self.nodes[0].name_show (name)
    assert nameData["ismine"]
    assert_equal (nameData["value"], newValue)

  def testAskOffer (self):
    self.log.info ("Testing trading by taking an ask offer...")

    # Build the ask transaction.
    price = 10
    name = "p/test"
    newValue = json.dumps ({"data": "ask taken"})
    ask = self.buildAsk (self.nodes[0], name, newValue, price)

    # Complete it by funding properly.
    tx = self.parseHexTx (ask)

    inp, inValue = self.findOutput (self.nodes[1], price)
    tx.vin.append (CTxIn (inp))

    change = inValue - price - FEE
    assert_greater_than (change, 0)
    changeAddr = self.nodes[1].getnewaddress ()
    tx.vout.append (self.buildTxOut (changeAddr, change))

    ask = tx.serialize ().hex ()

    # The transaction should be invalid if the amount received by the seller
    # is changed.
    tx = self.parseHexTx (ask)
    tx.vout[0].nValue = COIN
    txHex = tx.serialize ().hex ()
    signed = self.nodes[1].signrawtransactionwithwallet (txHex)
    assert not signed["complete"]

    # The transaction should be invalid if the name-output script is changed
    # to something else.
    wrongValue = json.dumps ({"data": "wrong"})
    addr = self.nodes[0].getnewaddress ()
    tx = self.parseHexTx (ask)
    tx.vout[0] = self.buildNameUpdate (name, wrongValue, addr, 10.01)
    txHex = tx.serialize ().hex ()
    signed = self.nodes[1].signrawtransactionwithwallet (txHex)
    assert not signed["complete"]

    # Take the ask successfully.
    signed = self.nodes[1].signrawtransactionwithwallet (ask)
    assert signed["complete"]
    oldValue = self.nodes[0].name_show (name)["value"]
    assert oldValue != newValue
    before = self.getBalances ()
    self.nodes[0].sendrawtransaction (signed["hex"])
    self.generate (1)
    self.sync_blocks ()
    nameData = self.nodes[0].name_show (name)
    assert nameData["ismine"]
    assert_equal (nameData["value"], newValue)

    # Recover the locked price and verify wallet balances.
    txid = self.nodes[0].name_update (name, "{}")
    self.generate (1, ind=0)
    feeUpdate = self.getTxFee (self.nodes[0], txid)
    assert_greater_than (0.001, feeUpdate)
    self.assertBalanceChange (before, [10 - feeUpdate, -10 - FEE])


if __name__ == '__main__':
  AtomicTradingTest ().main ()
