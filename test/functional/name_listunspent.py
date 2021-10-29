#!/usr/bin/env python3
# Copyright (c) 2018-2021 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for correct handling of names in "listunspent".

from test_framework.names import NameTestFramework, val
from test_framework.util import *

class NameListUnspentTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 2)

  def lookupName (self, ind, name, **kwargs):
    """Wrapper around lookup that gets txid and vout from the name."""

    pending = self.nodes[ind].name_pending (name)
    if len (pending) > 0:
      data = pending[0]
    else:
      data = self.nodes[ind].name_show (name)

    return self.lookup (ind, data['txid'], data['vout'], **kwargs)

  def lookup (self, ind, txid, vout,
              addressFilter=None,
              allowUnconfirmed=False,
              includeNames=False):
    """
    Runs listunspent on the given node and returns the unspent result matching
    the txid:vout combination or None.
    """

    minConf = 1
    if allowUnconfirmed:
      minConf = 0

    opt = {"includeNames": includeNames}
    res = self.nodes[ind].listunspent (minConf, None, addressFilter, None, opt)

    for out in res:
      if (out['txid'], out['vout']) == (txid, vout):
        return out
    return None

  def run_test (self):

    # Firstupdate the name and check that briefly.
    addrA = self.nodes[0].getnewaddress ()
    self.nodes[0].name_register ('x/testname', val ('value'),
                                 {"destAddress": addrA})
    self.generate (self.nodes[0], 1)
    unspent = self.lookupName (0, "x/testname", includeNames=True)
    assert unspent is not None
    assert 'nameOp' in unspent
    assert_equal (unspent['nameOp']['op'], 'name_register')
    assert_equal (unspent['nameOp']['name'], 'x/testname')

    # Update the name, sending to another node.
    addrB = self.nodes[1].getnewaddress ()
    self.nodes[0].name_update ("x/testname", val ("value"),
                               {"destAddress": addrB})
    self.sync_mempools ()

    # Node 0 should no longer have the unspent output.
    assert self.lookupName (0, "x/testname",
                            allowUnconfirmed=True,
                            includeNames=True) is None
    assert self.lookupName (0, "x/testname",
                            allowUnconfirmed=True,
                            includeNames=True) is None

    # Node 1 should now see the output as unconfirmed.
    assert self.lookupName (1, "x/testname", allowUnconfirmed=True) is None
    assert self.lookupName (1, "x/testname", includeNames=True) is None
    unspent = self.lookupName (1, "x/testname",
                               addressFilter=[addrB],
                               allowUnconfirmed=True,
                               includeNames=True)
    assert unspent is not None
    assert_equal (unspent['confirmations'], 0)
    assert 'nameOp' in unspent
    assert_equal (unspent['nameOp']['op'], 'name_update')
    assert_equal (unspent['nameOp']['name'], 'x/testname')

    # Mine blocks and verify node 1 seeing the name correctly.
    self.generate (self.nodes[1], 30)
    assert self.lookupName (1, "x/testname") is None
    assert self.lookupName (1, "x/testname",
                            addressFilter=[addrA],
                            includeNames=True) is None
    unspent = self.lookupName (1, "x/testname", includeNames=True)
    assert unspent is not None
    assert_equal (unspent['confirmations'], 30)
    assert 'nameOp' in unspent
    assert_equal (unspent['nameOp']['op'], 'name_update')
    assert_equal (unspent['nameOp']['name'], 'x/testname')

if __name__ == '__main__':
  NameListUnspentTest ().main ()
