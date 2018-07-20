#!/usr/bin/env python3
# Copyright (c) 2018 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test the "game-block" ZMQ notifications."""

from test_framework.test_framework import (
  BitcoinTestFramework,
  skip_if_no_bitcoind_zmq,
  skip_if_no_py3_zmq,
)
from test_framework.messages import (
  COIN,
  COutPoint,
  CTransaction,
  CTxIn,
  CTxOut,
)
from test_framework.util import (
  assert_equal,
  bytes_to_hex_str,
  hex_str_to_bytes,
)
from test_framework.script import (
  CScript,
  OP_TRUE,
)

from decimal import Decimal
import codecs
import json
import struct


def assertMove (obj, txid, name, move):
  """
  Utility method to assert the value of a move JSON without verifying
  the "out" field (which is unpredictable due to the change output).
  """

  assert_equal (obj["txid"], txid)
  assert_equal (obj["name"], name)
  assert_equal (obj["move"], move)


class GameSubscriber:

  def __init__ (self, ctx, addr, game):
    self.sequence = {}
    self.game = game

    import zmq
    self.socket = ctx.socket (zmq.SUB)
    self.socket.set (zmq.RCVTIMEO, 60000)
    self.socket.connect (addr)
    self.socket.setsockopt_string (zmq.SUBSCRIBE,
                                   "game-block-attach json %s" % game)
    self.socket.setsockopt_string (zmq.SUBSCRIBE,
                                   "game-block-detach json %s" % game)

  def receive (self):
    topic, body, seq = self.socket.recv_multipart ()

    topic = codecs.decode (topic, "ascii")
    parts = topic.split (" ")
    assert_equal (len (parts), 3)
    assert_equal (parts[1], "json")
    assert_equal (parts[2], self.game)
    assert parts[0] in ["game-block-attach", "game-block-detach"]

    # Sequence should be incremental for the full topic string.
    seqNum = struct.unpack ("<I", seq)[-1]
    if not topic in self.sequence:
      self.sequence[topic] = 0
    assert_equal (seqNum, self.sequence[topic])
    self.sequence[topic] += 1

    return topic, json.loads (codecs.decode (body, "ascii"))

  def assertNoMessage (self):
    import zmq
    try:
      _ = self.socket.recv (zmq.NOBLOCK)
      raise AssertionError ("expected no more messages, but got one")
    except zmq.error.Again:
      pass


class GameBlocksTest (BitcoinTestFramework):

  _address = "tcp://127.0.0.1:28332"

  def set_test_params (self):
    self.num_nodes = 1

  def setup_nodes (self):
    skip_if_no_py3_zmq ()
    skip_if_no_bitcoind_zmq (self)

    import zmq
    self.zmq_context = zmq.Context ()
    self.games = {}
    for g in ["a", "b", "ignored"]:
      self.games[g] = GameSubscriber (self.zmq_context, self._address, g)

    args = []
    args.append ("-zmqpubgameblocks=%s" % self._address)
    args.extend (["-trackgame=%s" % g for g in ["a", "b", "other"]])
    self.extra_args = [args]
    self.add_nodes (self.num_nodes, self.extra_args)
    self.start_nodes ()

    self.node = self.nodes[0]

  def run_test (self):
    try:
      self._test_currencyIgnored ()
      self._test_register ()
      self._test_blockHashes ()
      self._test_multipleUpdates ()
      self._test_moveWithCurrency ()
      self._test_reorg ()

      # After all the real tests, verify no more notifications are there.
      # This especially verifies that the "ignored" game we are subscribed to
      # has no notifications (because it is not tracked by the daemon).
      for _, sub in self.games.items ():
        sub.assertNoMessage ()
    finally:
      self.log.debug ("Destroying ZMQ context")
      self.zmq_context.destroy (linger=None)

  def _test_currencyIgnored (self):
    """
    Tests that a currency transaction does not show up as move in the
    notifications.
    """

    self.log.info ("Testing pure currency transaction...")

    addr = self.node.getnewaddress ()
    self.node.sendtoaddress (addr, 1.5)
    self.node.generate (1)

    for g in ["a", "b"]:
      _, data = self.games[g].receive ()
      assert_equal (data["moves"], [])

  def _test_register (self):
    """
    Registers test names and verifies that this already triggers
    a notification (not just name_update's).
    """

    self.log.info ("Registering names...")

    txid = self.node.name_register ("p/x", json.dumps ({"g":{"a":42}}))
    self.node.name_register ("p/y", json.dumps ({"g":{"other":False}}))
    self.node.generate (1)

    _, data = self.games["a"].receive ()
    assert_equal (len (data["moves"]), 1)
    assertMove (data["moves"][0], txid, "x", 42)

    _, data = self.games["b"].receive ()
    assert_equal (data["moves"], [])

  def _test_blockHashes (self):
    """
    Verifies the block hashes (parent and child) in the main message that
    is sent against the blockchain.
    """

    self.log.info ("Verifying block hashes...")

    parent = self.node.getbestblockhash ()
    child = self.node.generate (1)[0]
    expected = {"parent": parent, "child": child, "moves": []}

    for g in ["a", "b"]:
      _, data = self.games[g].receive ()
      assert_equal (data, expected)

  def _test_multipleUpdates (self):
    """
    Tests the case of multiple name updates in the block and multiple
    games referenced for a single name.
    """

    self.log.info ("Testing multiple updates in one block...")

    txidX = self.node.name_update ("p/x", json.dumps ({
      "stuff": "foo",
      "g":
        {
          "a": [42, False],
          "b": {"test": True},
        },
    }))
    txidY = self.node.name_update ("p/y", json.dumps ({"g":{"b":6.25}}))
    blk = self.node.generate (1)[0]

    # Get the order of our two transactions in the block, so that we can check
    # that the order in the notification matches it.
    txids = self.node.getblock (blk)["tx"][1:]
    assert_equal (set (txids), set ([txidX, txidY]))
    indX = txids.index (txidX)
    indY = txids.index (txidY)

    _, data = self.games["a"].receive ()
    assert_equal (len (data["moves"]), 1)
    assertMove (data["moves"][0], txidX, "x", [42, False])

    _, data = self.games["b"].receive ()
    assert_equal (len (data["moves"]), 2)
    assertMove (data["moves"][indX], txidX, "x", {"test": True})
    assertMove (data["moves"][indY], txidY, "y", 6.25)

  def _test_moveWithCurrency (self):
    """
    Sends currency to predefined addresses together with a move and checks
    the "out" field produced by the notifications.
    """

    self.log.info ("Sending move with currency...")

    addr1 = self.node.getnewaddress ()
    addr2 = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"

    # Send a move and spend coins in the same transaction.  We use a raw
    # transaction, so that we can actually send *two* outputs to the *same*
    # address.  This should work, and return it only once in the notification
    # with the total amount.  We use amounts with precision down to satoshis
    # to check this works correctly.

    hex1 = self.node.getaddressinfo (addr1)["scriptPubKey"]
    hex2 = self.node.getaddressinfo (addr2)["scriptPubKey"]
    scr1 = CScript (hex_str_to_bytes (hex1))
    scr2 = CScript (hex_str_to_bytes (hex2))

    tx = CTransaction ()
    name = self.node.name_show ("p/x")
    tx.vin.append (CTxIn (COutPoint (int (name["txid"], 16), name["vout"])))
    tx.vout.append (CTxOut (12345678, scr1))
    tx.vout.append (CTxOut (142424242, scr2))
    tx.vout.append (CTxOut (COIN, scr1))
    tx.vout.append (CTxOut (COIN // 2, CScript ([OP_TRUE])))
    tx.vout.append (CTxOut (COIN // 100, scr1))
    rawtx = bytes_to_hex_str (tx.serialize ())

    nameOp = {
      "op": "name_update",
      "name": "p/x",
      "value": json.dumps ({"g":{"a":"move"}}),
    }
    rawtx = self.node.namerawtransaction (rawtx, 4, nameOp)["hex"]

    rawtx = self.node.fundrawtransaction (rawtx)["hex"]
    signed = self.node.signrawtransactionwithwallet (rawtx)
    assert signed["complete"]
    txid = self.node.sendrawtransaction (signed["hex"])
    self.node.generate (1)

    _, data = self.games["a"].receive ()
    assert_equal (len (data["moves"]), 1)
    assertMove (data["moves"][0], txid, "x", "move")
    out = data["moves"][0]["out"]
    assert_equal (len (out), 3)
    quant = Decimal ('1.00000000')
    assert_equal (Decimal (out[addr1]).quantize (quant), Decimal ('1.12345678'))
    assert_equal (Decimal (out[addr2]).quantize (quant), Decimal ('1.42424242'))

    _, data = self.games["b"].receive ()
    assert_equal (data["moves"], [])

  def _test_reorg (self):
    """
    Produces a reorg and checks the resulting sequence of notifications
    (including those for detached blocks).
    """

    self.log.info ("Testing reorg...")

    def buildChain (n):
      blks = []
      attachA = []
      attachB = []
      for i in range (n):
        blks.extend (self.node.generate (1))
        topic, data = self.games["a"].receive ()
        assert_equal (topic, "game-block-attach json a")
        attachA.append (data)
        topic, data = self.games["b"].receive ()
        assert_equal (topic, "game-block-attach json b")
        attachB.append (data)
      return blks, attachA, attachB

    def verifyDetach (attachA, attachB):
      n = len (attachA)
      assert_equal (len (attachB), n)
      for i in range (n):
        topic, data = self.games["a"].receive ()
        assert_equal (topic, "game-block-detach json a")
        assert_equal (data, attachA[-i - 1])
        topic, data = self.games["b"].receive ()
        assert_equal (topic, "game-block-detach json b")
        assert_equal (data, attachB[-i - 1])

    # Start by building a long chain that we will later reorg to.  Include a
    # move (just because, not really important) and record the attach
    # notifications we get.
    self.node.name_update ("p/x", json.dumps ({"g":{"a": True}}))
    longBlks, longAttachA, longAttachB = buildChain (10)

    # Invalidate the first block, which should trigger detaches.
    self.node.invalidateblock (longBlks[0])
    verifyDetach (longAttachA, longAttachB)

    # Build a shorter chain.
    _, shortAttachA, shortAttachB = buildChain (5)

    # Trigger the reorg and verify the notifications.
    self.node.reconsiderblock (longBlks[0])
    verifyDetach (shortAttachA, shortAttachB)
    for i in range (len (longBlks)):
      topic, data = self.games["a"].receive ()
      assert_equal (topic, "game-block-attach json a")
      assert_equal (data, longAttachA[i])
      topic, data = self.games["b"].receive ()
      assert_equal (topic, "game-block-attach json b")
      assert_equal (data, longAttachB[i])


if __name__ == '__main__':
    GameBlocksTest ().main ()
