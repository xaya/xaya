#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC tests for the handling of names in the wallet.

from test_framework.names import NameTestFramework
from test_framework.util import *

from decimal import Decimal

nameFee = Decimal ("0.01")
txFee = Decimal ("0.001")
initialBalance = Decimal ("1250")
zero = Decimal ("0")

newFee = nameFee + txFee
firstFee = txFee
updFee = txFee

class NameWalletTest (NameTestFramework):

  spentA = zero
  spentB = zero

  # Set paytxfee to some value so that no estimated fees
  # are used and the amounts are predictable for the tests.
  def setup_nodes(self):
    args = ["-paytxfee=%s" % txFee]
    return start_nodes(4, self.options.tmpdir, [args] * 4)

  def checkBalance (self, ind, spent):
    """
    Check the balance of the node with index ind.  It should be
    the initial balance minus "spent".
    """

    bal = self.nodes[ind].getbalance ()
    assert_equal (bal, initialBalance - spent)

    assert_equal (self.nodes[ind].getbalance (""), bal)
    assert_equal (self.nodes[ind].getbalance ("*"), bal)
    assert_equal (self.nodes[ind].listaccounts (), {"":bal})

  def checkBalances (self, spentA = zero, spentB = zero):
    """
    Check balances of nodes 1 and 2.  The expected spent amounts
    for them are stored in self.spentA and self.spentB, and increased
    prior to the check by the arguments passed in.
    """

    self.spentA += spentA
    self.spentB += spentB

    self.checkBalance (2, self.spentA)
    self.checkBalance (3, self.spentB)

  def checkTx (self, ind, txid, amount, fee, details):
    """
    Call 'gettransaction' and compare the result to the
    expected data given in the arguments.  "details" is an array
    containing all the tx sent/received entries expected.
    Each array element is an array itself with the fields:

      [category, nameop, amount, fee]

    nameop can be None if no "name" key is expected.
    """

    data = self.nodes[ind].gettransaction (txid)
    assert_equal (data['amount'], amount)

    if fee is None:
      assert 'fee' not in data
    else:
      assert_equal (data['fee'], fee)

    # Bring the details returned in the same format as our expected
    # argument.  Furthermore, check that each entry has an address
    # set (but don't compare the address, since we don't know it).
    detailsGot = []
    for d in data['details']:
      assert 'address' in d
      if 'name' in d:
        nameOp = d['name']
        if nameOp[:3] == 'new':
          nameOp = 'new'
      else:
        nameOp = None
      if 'fee' in d:
        fee = d['fee']
      else:
        fee = None
      detailsGot.append ([d['category'], nameOp, d['amount'], fee])

    # Compare.  Sort to get rid of differences in the order.
    detailsGot.sort ()
    details.sort ()
    assert_equal (detailsGot, details)

  def run_test (self):
    NameTestFramework.run_test (self)
    
    # Note that the next 50 maturing blocks will be for nodes 0 and 1.
    # Thus we use 2 and 3 for the tests, because their balance
    # will stay constant over time except for our explicit transactions.

    self.checkBalances ()

    # Register and update a name.  Check changes to the balance.
    newA = self.nodes[2].name_new ("name-a")
    self.generate (0, 5)
    self.checkBalances (newFee)
    firstA = self.firstupdateName (2, "name-a", newA, "value")
    self.generate (0, 10)
    self.checkBalances (firstFee)
    updA = self.nodes[2].name_update ("name-a", "new value")
    self.generate (0, 1)
    self.checkBalances (updFee)

    # Check the transactions.
    self.checkTx (2, newA[0], zero, -newFee,
                  [['send', 'new', zero, -newFee]])
    self.checkTx (2, firstA, zero, -firstFee,
                  [['send', 'update: name-a', zero, -firstFee]])
    self.checkTx (2, updA, zero, -updFee,
                  [['send', 'update: name-a', zero, -updFee]])

    # Send a name from 1 to 2 by firstupdate and update.
    addrB = self.nodes[3].getnewaddress ()
    newB = self.nodes[2].name_new ("name-b")
    newC = self.nodes[2].name_new ("name-c")
    self.generate (0, 5)
    self.checkBalances (2 * newFee)
    firstB = self.firstupdateName (2, "name-b", newB, "value", addrB)
    firstC = self.firstupdateName (2, "name-c", newC, "value")
    self.generate (0, 10)
    self.checkBalances (2 * firstFee)
    updC = self.nodes[2].name_update ("name-c", "new value", addrB)
    self.generate (0, 1)
    self.checkBalances (updFee)

    # Check the receiving transactions on B.
    self.checkTx (3, firstB, zero, None,
                  [['receive', 'update: name-b', zero, None]])
    self.checkTx (3, updC, zero, None,
                  [['receive', 'update: name-c', zero, None]])

    # Use the rawtx API to build a simultaneous name update and currency send.
    # This is done as an atomic name trade.  Note, though, that the
    # logic is a bit confused by "coin join" transactions and thus
    # possibly not exactly what one would expect.

    price = Decimal ("1.0")
    txid = self.atomicTrade ("name-a", "enjoy", price, 2, 3)
    self.generate (0, 1)

    self.checkBalances (-price, price)
    self.checkTx (2, txid, price, None,
                  [['receive', None, price, None]])
    self.checkTx (3, txid, -price, zero,
                  [['send', None, -price, zero],
                   ['send', 'update: name-a', zero, zero]])

if __name__ == '__main__':
  NameWalletTest ().main ()
