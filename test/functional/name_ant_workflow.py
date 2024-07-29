#!/usr/bin/env python3
# Copyright (c) 2020-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the full workflow of doing atomic name trades using the
# suggested RPC/PSBT interface.

from test_framework.names import NameTestFramework
from test_framework.util import *


class NameAntWorkflowTest (NameTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.setup_name_test ([[]] * 2)

  def run_test (self):
    self.generate (self.nodes[1], 10)
    self.sync_blocks ()
    self.generate (self.nodes[0], 150)

    new = self.nodes[0].name_new ("x/name")
    self.generate (self.nodes[0], 10)
    self.firstupdateName (0, "x/name", new, "value")
    self.generate (self.nodes[0], 5)
    self.sync_blocks ()
    self.checkName (0, "x/name", "value", None, False)

    # We construct the base transaction first, with just the name input,
    # name output (including the operation) and the payment output.
    # We then apply fundrawtransaction to add the necessary input and change
    # for the payment; this needs solving data for the non-wallet input
    # (the name) to be passed in, which in this case is the pubkey for the
    # name input address.

    addrPayment = self.nodes[0].getnewaddress ()
    addrNm = self.nodes[1].getnewaddress ()

    nmData = self.nodes[0].name_show ("x/name")
    addrNmOld = nmData["address"]
    pubKey = self.nodes[0].getaddressinfo (addrNmOld)["pubkey"]

    tx = self.nodes[1].createrawtransaction ([nmData], {
      addrPayment: 10,
      addrNm: 0.01,
    })
    vout = self.rawtxOutputIndex (1, tx, addrNm)
    nameOp = {
      "op": "name_update",
      "name": "x/name",
      "value": "updated",
    }
    tx = self.nodes[1].namerawtransaction (tx, vout, nameOp)["hex"]

    options = {
      "solving_data": {
        "pubkeys": [pubKey],
      },
    }
    tx = self.nodes[1].fundrawtransaction (tx, options)["hex"]
    psbt = self.nodes[1].converttopsbt (tx)

    # Sign and broadcast the partial tx.
    sign1 = self.nodes[0].walletprocesspsbt (psbt)
    sign2 = self.nodes[1].walletprocesspsbt (psbt)
    combined = self.nodes[1].combinepsbt ([sign1["psbt"], sign2["psbt"]])
    tx = self.nodes[1].finalizepsbt (combined)
    assert_equal (tx["complete"], True)
    self.nodes[0].sendrawtransaction (tx["hex"])
    self.generate (self.nodes[0], 1)
    data = self.checkName (0, "x/name", "updated", None, False)
    assert_equal (data["address"], addrNm)


if __name__ == '__main__':
  NameAntWorkflowTest (__file__).main ()
