#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# General code for Namecoin tests.

from test_framework import BitcoinTestFramework
from util import assert_equal, sync_blocks, sync_mempools

# TODO: test reorgs when the test framework supports it
# TODO: test raw tx decoding and creation (-> ANTPY)
# TODO: test advanced name access when implemented (name_filter & co)

class NameTestFramework (BitcoinTestFramework):

  def firstupdateName (self, node, name, newData, value, toAddr = None):
    """
    Utility routine to perform a name_firstupdate command.  The rand
    and txid are taken from 'newData', as it is returned by name_new.
    """

    if toAddr is None:
      return node.name_firstupdate (name, newData[1], newData[0], value)
    
    return node.name_firstupdate (name, newData[1], newData[0], value, toAddr)

  def generate (self, nodes, ind, blocks):
    """
    Generate blocks and sync all nodes.
    """

    sync_mempools (nodes)
    nodes[ind].setgenerate (True, blocks)
    sync_blocks (nodes)

  def checkName (self, node, name, value, expiresIn, expired):
    """
    Query a name with name_show and check that certain data fields
    match the expectations.  Returns the full JSON object.
    """

    data = node.name_show (name)
    assert_equal (data['name'], name)
    assert_equal (data['value'], value)
    if (expiresIn is not None):
      assert_equal (data['expires_in'], expiresIn)
    assert_equal (data['expired'], expired)

    return data
