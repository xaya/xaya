#!/usr/bin/env python3
# Copyright (c) 2020-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC tests for name operations with PSBTs.

from test_framework.names import NameTestFramework, val
from test_framework.util import *

from decimal import Decimal

class NamePsbtTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 2)

  def run_test (self):

    # Go through the full name "life cycle" (name_register and
    # name_update) with the PSBT interface.
    regOp = {"op": "name_register", "name": "d/my-name",
             "value": val ("first value")}
    regAddr = self.nodes[0].getnewaddress ()
    regOutp, regData = self.rawNameOp (0, None, regAddr, regOp)
    self.generate (self.nodes[0], 1)
    self.checkName (0, "d/my-name", val ("first value"))

    updOp = {"op": "name_update", "name": "d/my-name",
             "value": val ("new value")}
    updAddr = self.nodes[0].getnewaddress ()
    _, updData = self.rawNameOp (0, regOutp, updAddr, updOp)
    self.generate (self.nodes[0], 1)
    self.checkName (0, "d/my-name", val ("new value"))

    # Decode name_register.
    data = self.decodePsbt (self.nodes[0], regData["psbt"])
    assert_equal (data["op"], "name_register")
    assert_equal (data["name"], "d/my-name")
    assert_equal (data["value"], val ("first value"))

    # Decode name_update.
    data = self.decodePsbt (self.nodes[0], updData["psbt"])
    assert_equal (data["op"], "name_update")
    assert_equal (data["name"], "d/my-name")
    assert_equal (data["value"], val ("new value"))

    # Verify range check of vout in namepsbt.
    tx = self.nodes[0].createpsbt ([], {})
    assert_raises_rpc_error (-8, "vout is out of range",
                             self.nodes[0].namepsbt, tx, 0, {})
    assert_raises_rpc_error (-8, "vout is out of range",
                             self.nodes[0].namepsbt, tx, -1, {})

  def decodePsbt (self, node, psbt):
    """
    Decodes a PSBT and finds the output that is a name operation.  Returns
    the decoded nameop entry.
    """

    data = node.decodepsbt (psbt)["tx"]

    res = None
    for out in data["vout"]:
      if "nameOp" in out["scriptPubKey"]:
        assert_equal (res, None)
        res = out["scriptPubKey"]["nameOp"]

        # Extra check:  Verify that the address is decoded correctly.
        addr = out["scriptPubKey"]["address"]
        assert_equal (out["scriptPubKey"]["type"], "pubkeyhash")
        validation = node.validateaddress (addr)
        assert_equal (validation["isvalid"], True)

    assert res is not None
    return res

  def rawNameOp (self, ind, nameIn, toAddr, op):
    """
    Utility method to construct and send a name-operation transaction with
    the PSBT interface.  It uses the provided input (if not None)
    for the name and finds other inputs to fund the tx.  It sends the name
    to toAddr with the operation defined by op.
    """

    fee = Decimal ("0.001")
    nameAmount = Decimal ("0.01")

    node = self.nodes[ind]
    changeAddr = node.getrawchangeaddress ()

    vin = []
    vout = []
    for u in node.listunspent ():
      if u["amount"] > fee + nameAmount:
        vin.append (u)
        vout.append ({changeAddr: u["amount"] - fee - nameAmount})
        break

    assert len (vin) > 0, "found no suitable funding input"

    if nameIn is not None:
      vin.append (nameIn)
    nameInd = len (vout)
    vout.append ({toAddr: nameAmount})

    tx = node.createpsbt (vin, vout)
    nameTx = node.namepsbt (tx, nameInd, op)

    tx = node.walletprocesspsbt (nameTx["psbt"])
    assert_equal (tx["complete"], True)
    tx = node.finalizepsbt (tx["psbt"])
    txid = node.sendrawtransaction (tx["hex"])

    return {"txid": txid, "vout": nameInd}, nameTx


if __name__ == '__main__':
  NamePsbtTest ().main ()
